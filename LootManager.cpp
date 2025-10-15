#include "LootManager.h"
#include "Settings.h"
#include <random>

void LootManager::Register() {
    auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (eventSource) {
        eventSource->AddEventSink<RE::TESDeathEvent>(this);
    }
}

RE::BSEventNotifyControl LootManager::ProcessEvent(
    const RE::TESDeathEvent* a_event,
    RE::BSTEventSource<RE::TESDeathEvent>*) {
    
    if (!enabled || !a_event || !a_event->actorDying) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    auto* actor = a_event->actorDying->As<RE::Actor>();
    
    if (actor && ShouldProcessActor(actor)) {
        ProcessActorDeath(actor);
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

void LootManager::ProcessActorDeath(RE::Actor* a_actor) {
    // Process after death animation completes
    std::thread([this, actorHandle = a_actor->GetHandle()]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        if (auto actor = actorHandle.get().get()) {
            MarkNonLootableItems(actor);
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

void LootManager::MarkNonLootableItems(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    // Build list of equipped items to preserve visual appearance
    std::set<RE::FormID> equippedItems;
    
    // Check worn armor
    auto* wornArmor = a_actor->GetWornArmor(RE::BIPED_MODEL::BipedObjectSlot::kBody);
    if (wornArmor) equippedItems.insert(wornArmor->GetFormID());
    
    wornArmor = a_actor->GetWornArmor(RE::BIPED_MODEL::BipedObjectSlot::kHead);
    if (wornArmor) equippedItems.insert(wornArmor->GetFormID());
    
    wornArmor = a_actor->GetWornArmor(RE::BIPED_MODEL::BipedObjectSlot::kHands);
    if (wornArmor) equippedItems.insert(wornArmor->GetFormID());
    
    wornArmor = a_actor->GetWornArmor(RE::BIPED_MODEL::BipedObjectSlot::kFeet);
    if (wornArmor) equippedItems.insert(wornArmor->GetFormID());
    
    // Check equipped weapons
    auto* equippedRight = a_actor->GetEquippedObject(false);
    if (equippedRight) equippedItems.insert(equippedRight->GetFormID());
    
    auto* equippedLeft = a_actor->GetEquippedObject(true);
    if (equippedLeft) equippedItems.insert(equippedLeft->GetFormID());
    
    // Process inventory
    auto inventory = a_actor->GetInventory();
    std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> itemsToRemove;
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) continue;
        
        // Always keep gold
        if (item->IsGold()) continue;
        
        // Always keep keys
        if (item->GetFormType() == RE::FormType::Misc) {
            if (auto* miscItem = item->As<RE::TESObjectMISC>()) {
                if (miscItem->GetFullName() && 
                    std::string_view(miscItem->GetFullName()).find("Key") != std::string_view::npos) {
                    continue;
                }
            }
        }
        
        // Keep equipped items for visual appearance
        if (equippedItems.find(item->GetFormID()) != equippedItems.end()) {
            continue;
        }
        
        // Determine removal count for non-equipped items
        std::int32_t removeCount = 0;
        
        if (count > 1) {
            std::random_device rd;
            std::mt19937 gen(rd());
            
            for (std::int32_t i = 0; i < count; ++i) {
                if (!ShouldDropItem(item, a_actor)) {
                    removeCount++;
                }
            }
        } else if (!ShouldDropItem(item, a_actor)) {
            removeCount = 1;
        }
        
        if (removeCount > 0) {
            itemsToRemove.push_back({item, removeCount});
        }
    }
    
    // Remove non-lootable unequipped items
    for (const auto& [item, count] : itemsToRemove) {
        a_actor->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
    }
}

bool LootManager::ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor) {
    if (!a_item) return true;
    
    if (a_item->IsGold()) {
        return true;
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