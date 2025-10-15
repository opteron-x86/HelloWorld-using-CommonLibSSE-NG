#include "LootManager.h"
#include "Settings.h"
#include <random>

void LootManager::Register() {
    auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (eventSource) {
        eventSource->AddEventSink(this);
    }
}

RE::BSEventNotifyControl LootManager::ProcessEvent(
    const RE::TESDeathEvent* a_event,
    RE::BSTEventSource<RE::TESDeathEvent>*) {
    
    if (!enabled || !a_event || !a_event->actorDying) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    auto* actor = a_event->actorDying->As<RE::Actor>();
    auto* killer = a_event->actorKiller ? a_event->actorKiller->As<RE::Actor>() : nullptr;
    
    if (actor && ShouldProcessActor(actor)) {
        ProcessActorDeath(actor, killer);
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

void LootManager::ProcessActorDeath(RE::Actor* a_actor, RE::Actor* a_killer) {
    auto actorHandle = a_actor->GetHandle();
    
    std::thread([this, actorHandle]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto* actor = actorHandle.get().get();
        
        if (actor) {
            std::lock_guard<std::mutex> lock(processingMutex);
            CreateLootContainer(actor);
        }
    }).detach();
}

bool LootManager::ShouldProcessActor(RE::Actor* a_actor) {
    if (!a_actor || a_actor->IsPlayerRef()) {
        return false;
    }
    
    if (a_actor->IsEssential()) {
        return false;
    }
    
    auto* base = a_actor->GetActorBase();
    if (base && base->IsSummonable()) {
        return false;
    }
    
    return true;
}

RE::TESObjectREFR* LootManager::GetOrCreateContainerBase() {
    if (!containerBase) {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return nullptr;
        
        // Find or create a generic container base object
        // Using a sack as it's small and unobtrusive
        containerBase = dataHandler->LookupForm<RE::TESObjectCONT>(0x000B876F, "Skyrim.esm"); // Sack01
        
        if (!containerBase) {
            // Fallback to any container
            auto& containers = dataHandler->GetFormArray<RE::TESObjectCONT>();
            if (!containers.empty()) {
                containerBase = containers[0];
            }
        }
    }
    
    return containerBase;
}

void LootManager::CreateLootContainer(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    auto* containerBase = GetOrCreateContainerBase();
    if (!containerBase) {
        logger::error("Failed to find container base form");
        return;
    }
    
    auto inventory = a_actor->GetInventory();
    std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> lootableItems;
    
    // Determine which items should be lootable
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) continue;
        
        // Gold always drops
        if (item->IsGold()) {
            lootableItems.push_back({item, count});
            continue;
        }
        
        // Check if item is equipped
        bool isEquipped = false;
        if (entry && entry->extraLists) {
            for (auto* extraList : *entry->extraLists) {
                if (extraList && extraList->HasType(RE::ExtraDataType::kWorn) || 
                    extraList->HasType(RE::ExtraDataType::kWornLeft)) {
                    isEquipped = true;
                    break;
                }
            }
        }
        
        // For equipped items, always check drop chance
        // For unequipped items, check drop chance per item in stack
        if (isEquipped) {
            if (ShouldDropItem(item, a_actor)) {
                lootableItems.push_back({item, count});
            }
        } else {
            // For stackable items, roll for each one
            std::int32_t dropCount = 0;
            
            if (count > 1) {
                for (std::int32_t i = 0; i < count; ++i) {
                    if (ShouldDropItem(item, a_actor)) {
                        dropCount++;
                    }
                }
            } else {
                if (ShouldDropItem(item, a_actor)) {
                    dropCount = 1;
                }
            }
            
            if (dropCount > 0) {
                lootableItems.push_back({item, dropCount});
            }
        }
    }
    
    // Only create container if there are lootable items
    if (lootableItems.empty()) {
        // Disable looting on the corpse
        a_actor->SetActivate(false);
        return;
    }
    
    // Create container at actor's position
    auto* cell = a_actor->GetParentCell();
    if (!cell) return;
    
    auto position = a_actor->GetPosition();
    auto angle = a_actor->GetAngle();
    
    auto* container = cell->PlaceObjectAtMe(containerBase, false);
    if (!container) {
        logger::error("Failed to create loot container");
        return;
    }
    
    // Position container at corpse
    container->SetPosition(position);
    container->SetAngle(angle);
    
    // Make container invisible and enable it
    container->SetDisplayName(a_actor->GetDisplayFullName(), true);
    
    // Transfer lootable items to container
    for (const auto& [item, count] : lootableItems) {
        // Remove from corpse without dropping
        a_actor->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
        
        // Add to container
        container->AddObjectToContainer(item, nullptr, count, nullptr);
    }
    
    // Disable looting on the corpse to force interaction with container
    a_actor->SetActivate(false);
    
    // Make sure container is enabled and can be activated
    container->Enable(false);
}

bool LootManager::ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor) {
    if (!a_item) return true;
    
    if (a_item->IsGold()) {
        return true;
    }
    
    // Quest items always drop to prevent quest breakage
    if (a_item->GetFormType() == RE::FormType::Misc) {
        if (auto* miscItem = a_item->As<RE::TESObjectMISC>()) {
            if (miscItem && miscItem->GetFullName() && 
                std::string_view(miscItem->GetFullName()).find("Key") != std::string_view::npos) {
                return true;
            }
        }
    }
    
    float dropChance = GetDropChance(a_item, a_actor);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    
    return dist(gen) < dropChance;
}

float LootManager::GetDropChance(RE::TESBoundObject* a_item, RE::Actor* a_actor) {
    auto* settings = Settings::GetSingleton();
    
    float baseChance = settings->defaultDropChance;
    
    switch (a_item->GetFormType()) {
    case RE::FormType::Armor:
        baseChance = settings->armorDropChance;
        break;
        
    case RE::FormType::Weapon:
        baseChance = settings->weaponDropChance;
        break;
        
    case RE::FormType::Ammo:
        baseChance = settings->ammoDropChance;
        break;
        
    case RE::FormType::AlchemyItem:
        baseChance = settings->potionDropChance;
        break;
        
    case RE::FormType::Ingredient:
        baseChance = settings->ingredientDropChance;
        break;
        
    case RE::FormType::Book:
    case RE::FormType::Scroll:
        baseChance = settings->bookDropChance;
        break;
        
    case RE::FormType::Misc:
        baseChance = settings->miscDropChance;
        break;
        
    case RE::FormType::SoulGem:
        baseChance = settings->soulgemDropChance;
        break;
    }
    
    float multiplier = 1.0f;
    
    if (auto* enchantable = a_item->As<RE::TESEnchantableForm>()) {
        if (enchantable->formEnchanting) {
            multiplier *= settings->enchantedMultiplier;
        }
    }
    
    if (a_item->GetGoldValue() > 1000) {
        multiplier *= settings->uniqueMultiplier;
    }
    
    if (a_actor) {
        auto* base = a_actor->GetActorBase();
        if (base) {
            if (a_actor->GetLevel() > 30) {
                multiplier *= settings->bossMultiplier;
            }
            else if (base->GetRace() && base->GetRace()->formEditorID.contains("Bandit")) {
                multiplier *= settings->banditMultiplier;
            }
        }
    }
    
    return std::min(1.0f, baseChance * multiplier);
}