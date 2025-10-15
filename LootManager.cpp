#include "LootManager.h"
#include "Settings.h"
#include <random>
#include <unordered_set>

void LootManager::Register() {
    auto* scriptEventSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (scriptEventSource) {
        scriptEventSource->AddEventSink(this);
    }
    
    // Register for container changed events to prevent looting blocked items
    if (auto* ui = RE::UI::GetSingleton()) {
        ui->GetEventSource<RE::TESContainerChangedEvent>()->AddEventSink(this);
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

RE::BSEventNotifyControl LootManager::ProcessEvent(
    const RE::TESContainerChangedEvent* a_event,
    RE::BSTEventSource<RE::TESContainerChangedEvent>*) {
    
    if (!a_event) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    // Check if player is taking an item from a corpse
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || a_event->newContainer != player->GetFormID()) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    // Get the source actor
    auto* sourceForm = RE::TESForm::LookupByID(a_event->oldContainer);
    auto* sourceActor = sourceForm ? sourceForm->As<RE::Actor>() : nullptr;
    
    if (!sourceActor || !sourceActor->IsDead()) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    // Check if this item is blocked
    auto* item = RE::TESForm::LookupByID<RE::TESBoundObject>(a_event->baseObj);
    if (item && IsItemBlocked(sourceActor, item)) {
        // Return item to corpse
        SKSE::GetTaskInterface()->AddTask([sourceActor, item, count = a_event->itemCount]() {
            if (sourceActor && item) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (player) {
                    player->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, sourceActor);
                }
            }
        });
        
        // Show message to player
        RE::DebugNotification("This item cannot be looted.");
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
            FilterInventory(actor);
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

void LootManager::FilterInventory(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    auto inventory = a_actor->GetInventory();
    std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> itemsToRemove;
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) continue;
        
        bool isEquipped = IsItemEquipped(a_actor, item);
        bool shouldDrop = ShouldDropItem(item, a_actor);
        
        // Always allow gold
        if (item->IsGold()) {
            continue;
        }
        
        // Handle equipped items differently from unequipped
        if (isEquipped && !shouldDrop) {
            // Keep equipped but block from looting
            BlockItem(a_actor, item);
        } else if (!isEquipped) {
            // For unequipped items, determine how many to remove
            std::int32_t removeCount = 0;
            
            if (count > 1) {
                std::random_device rd;
                std::mt19937 gen(rd());
                
                for (std::int32_t i = 0; i < count; ++i) {
                    if (!ShouldDropItem(item, a_actor)) {
                        removeCount++;
                    }
                }
            } else if (!shouldDrop) {
                removeCount = 1;
            }
            
            if (removeCount > 0) {
                itemsToRemove.push_back({item, removeCount});
            }
        }
    }
    
    // Remove unequipped filtered items
    for (const auto& [item, count] : itemsToRemove) {
        a_actor->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
    }
}

bool LootManager::IsItemEquipped(RE::Actor* a_actor, RE::TESBoundObject* a_item) {
    if (!a_actor || !a_item) return false;
    
    auto inventory = a_actor->GetInventory();
    auto it = inventory.find(a_item);
    
    if (it != inventory.end()) {
        auto& [count, entry] = it->second;
        if (entry && entry->extraLists) {
            for (auto* xList : *entry->extraLists) {
                if (xList && xList->HasType(RE::ExtraDataType::kWorn)) {
                    return true;
                }
            }
        }
    }
    
    return false;
}

bool LootManager::ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor) {
    if (!a_item) return true;
    
    if (a_item->IsGold()) {
        return true;
    }
    
    // Always drop keys to prevent quest issues
    if (a_item->GetFormType() == RE::FormType::Key) {
        return true;
    }
    
    // Check for keys in misc items
    if (a_item->GetFormType() == RE::FormType::Misc) {
        if (auto* miscItem = a_item->As<RE::TESObjectMISC>()) {
            if (miscItem && miscItem->GetFullName()) {
                std::string_view name(miscItem->GetFullName());
                if (name.find("Key") != std::string_view::npos || 
                    name.find("key") != std::string_view::npos) {
                    return true;
                }
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
            } else if (base->GetRace() && base->GetRace()->formEditorID.contains("Bandit")) {
                multiplier *= settings->banditMultiplier;
            }
        }
    }
    
    return std::min(1.0f, baseChance * multiplier);
}

void LootManager::BlockItem(RE::Actor* a_actor, RE::TESBoundObject* a_item) {
    if (!a_actor || !a_item) return;
    
    std::lock_guard<std::mutex> lock(blockedItemsMutex);
    blockedItems[a_actor->GetFormID()].insert(a_item->GetFormID());
}

bool LootManager::IsItemBlocked(RE::Actor* a_actor, RE::TESBoundObject* a_item) {
    if (!a_actor || !a_item) return false;
    
    std::lock_guard<std::mutex> lock(blockedItemsMutex);
    
    auto it = blockedItems.find(a_actor->GetFormID());
    if (it == blockedItems.end()) {
        return false;
    }
    
    return it->second.count(a_item->GetFormID()) > 0;
}

void LootManager::CleanupBlockedItems(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    std::lock_guard<std::mutex> lock(blockedItemsMutex);
    blockedItems.erase(a_actor->GetFormID());
}