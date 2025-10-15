#include "LootManager.h"
#include "Settings.h"
#include <random>

void LootManager::Register() {
    auto* eventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
    if (eventSourceHolder) {
        eventSourceHolder->AddEventSink<RE::TESDeathEvent>(this);
        eventSourceHolder->AddEventSink<RE::TESContainerChangedEvent>(this);
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

RE::BSEventNotifyControl LootManager::ProcessEvent(
    const RE::TESContainerChangedEvent* a_event,
    RE::BSTEventSource<RE::TESContainerChangedEvent>*) {
    
    if (!a_event || !a_event->baseObj) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || a_event->newContainer != player->GetFormID()) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    // Player is taking item from something
    RE::FormID sourceID = a_event->oldContainer;
    RE::FormID itemID = a_event->baseObj;
    std::int32_t count = a_event->itemCount;
    
    // Check if this item is lootable from this source
    if (!IsItemLootable(sourceID, itemID, count)) {
        // Return item to source
        auto* sourceRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(sourceID);
        if (sourceRef) {
            player->RemoveItem(
                RE::TESForm::LookupByID<RE::TESBoundObject>(itemID),
                count,
                RE::ITEM_REMOVE_REASON::kStoreInContainer,
                nullptr,
                sourceRef);
            
            RE::DebugNotification("You cannot loot that item.");
        }
    } else {
        // Decrement available loot count
        DecrementLootableCount(sourceID, itemID, count);
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

void LootManager::ProcessActorDeath(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    auto lootData = DetermineLootableItems(a_actor);
    
    std::lock_guard<std::mutex> lock(mapMutex);
    lootDataMap[a_actor->GetFormID()] = std::move(lootData);
    
    RE::ConsoleLog::GetSingleton()->Print("Loot determined for actor");
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

LootData LootManager::DetermineLootableItems(RE::Actor* a_actor) {
    LootData result;
    
    if (!a_actor) return result;
    
    auto inventory = a_actor->GetInventory();
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) continue;
        
        RE::FormID itemID = item->GetFormID();
        
        // Gold always lootable
        if (item->IsGold()) {
            result.lootableItems.insert(itemID);
            result.lootableCounts[itemID] = count;
            continue;
        }
        
        // Roll for each item stack
        std::int32_t lootableCount = 0;
        for (std::int32_t i = 0; i < count; ++i) {
            if (ShouldDropItem(item, a_actor)) {
                lootableCount++;
            }
        }
        
        if (lootableCount > 0) {
            result.lootableItems.insert(itemID);
            result.lootableCounts[itemID] = lootableCount;
        }
    }
    
    return result;
}

bool LootManager::ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor) {
    if (!a_item) return false;
    
    if (a_item->IsGold()) return true;
    
    // Keys always drop
    if (a_item->GetFormType() == RE::FormType::KeyMaster) {
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
    
    if (a_actor && a_actor->GetLevel() > 30) {
        multiplier *= settings->bossMultiplier;
    }
    
    return std::min(1.0f, baseChance * multiplier);
}

bool LootManager::IsItemLootable(RE::FormID actorID, RE::FormID itemID, std::int32_t count) {
    std::lock_guard<std::mutex> lock(mapMutex);
    
    auto it = lootDataMap.find(actorID);
    if (it == lootDataMap.end()) {
        return true;  // Not tracked, allow normal looting
    }
    
    auto& lootData = it->second;
    
    if (lootData.lootableItems.find(itemID) == lootData.lootableItems.end()) {
        return false;  // Item not in lootable set
    }
    
    auto countIt = lootData.lootableCounts.find(itemID);
    if (countIt == lootData.lootableCounts.end() || countIt->second < count) {
        return false;  // Not enough lootable count
    }
    
    return true;
}

void LootManager::DecrementLootableCount(RE::FormID actorID, RE::FormID itemID, std::int32_t count) {
    std::lock_guard<std::mutex> lock(mapMutex);
    
    auto it = lootDataMap.find(actorID);
    if (it == lootDataMap.end()) {
        return;
    }
    
    auto& lootData = it->second;
    auto countIt = lootData.lootableCounts.find(itemID);
    
    if (countIt != lootData.lootableCounts.end()) {
        countIt->second -= count;
        
        if (countIt->second <= 0) {
            lootData.lootableItems.erase(itemID);
            lootData.lootableCounts.erase(countIt);
        }
    }
    
    // Clean up empty loot data
    if (lootData.lootableItems.empty()) {
        lootDataMap.erase(it);
    }
}