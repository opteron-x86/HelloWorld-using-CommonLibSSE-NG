#include "LootManager.h"
#include "Settings.h"
#include <random>

void LootManager::Register() {
    auto* scriptEventSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (scriptEventSource) {
        scriptEventSource->AddEventSink<RE::TESDeathEvent>(this);
        scriptEventSource->AddEventSink<RE::TESOpenCloseEvent>(this);
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
    const RE::TESOpenCloseEvent* a_event,
    RE::BSTEventSource<RE::TESOpenCloseEvent>*) {
    
    if (!a_event || !a_event->ref) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    RE::ConsoleLog::GetSingleton()->Print("OpenClose event: ref=%X, opened=%d", 
                                          a_event->ref->GetFormID(), a_event->opened);
    
    auto* actor = a_event->ref->As<RE::Actor>();
    if (!actor) {
        RE::ConsoleLog::GetSingleton()->Print("  Not an actor");
        return RE::BSEventNotifyControl::kContinue;
    }
    
    if (!actor->IsDead()) {
        RE::ConsoleLog::GetSingleton()->Print("  Actor not dead");
        return RE::BSEventNotifyControl::kContinue;
    }
    
    RE::ConsoleLog::GetSingleton()->Print("  Dead actor detected, opened=%d", a_event->opened);
    
    if (a_event->opened) {
        RemoveNonLootableItems(actor);
    } else {
        RestoreNonLootableItems(actor);
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

void LootManager::ProcessActorDeath(RE::Actor* a_actor) {
    auto actorHandle = a_actor->GetHandle();
    
    std::thread([this, actorHandle]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto* actor = actorHandle.get().get();
        if (actor) {
            DetermineLootableItems(actor);
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

void LootManager::DetermineLootableItems(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    auto inventory = a_actor->GetInventory();
    
    LootableItemData lootData;
    lootData.timestamp = std::chrono::steady_clock::now();
    
    RE::ConsoleLog::GetSingleton()->Print("Processing actor: %X with %zu items", 
                                          a_actor->GetFormID(), inventory.size());
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) {
            continue;
        }
        
        if (item->IsGold()) {
            lootData.lootableItems.insert(item->GetFormID());
            continue;
        }
        
        bool shouldDrop = false;
        if (count > 1) {
            std::random_device rd;
            std::mt19937 gen(rd());
            
            for (std::int32_t i = 0; i < count; ++i) {
                if (ShouldDropItem(item, a_actor)) {
                    shouldDrop = true;
                    break;
                }
            }
        } else {
            shouldDrop = ShouldDropItem(item, a_actor);
        }
        
        if (shouldDrop) {
            lootData.lootableItems.insert(item->GetFormID());
        }
        
        RE::ConsoleLog::GetSingleton()->Print("  Item %X (%s): %s", 
                                              item->GetFormID(),
                                              item->GetName(),
                                              shouldDrop ? "LOOTABLE" : "BLOCKED");
    }
    
    std::lock_guard<std::mutex> lock(dataMutex);
    RE::ConsoleLog::GetSingleton()->Print("Storing %zu lootable items for actor %X", 
                                          lootData.lootableItems.size(), a_actor->GetFormID());
    actorLootData[a_actor->GetFormID()] = std::move(lootData);
}

void LootManager::RemoveNonLootableItems(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    std::lock_guard<std::mutex> lock(dataMutex);
    
    auto it = actorLootData.find(a_actor->GetFormID());
    if (it == actorLootData.end()) {
        return;
    }
    
    auto& lootData = it->second;
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lootData.timestamp);
    
    if (elapsed.count() > 10) {
        actorLootData.erase(it);
        return;
    }
    
    lootData.removedItems.clear();
    
    auto inventory = a_actor->GetInventory();
    
    RE::ConsoleLog::GetSingleton()->Print("Container opened for actor %X", a_actor->GetFormID());
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) {
            continue;
        }
        
        if (item->IsGold()) {
            continue;
        }
        
        if (!lootData.lootableItems.contains(item->GetFormID())) {
            RE::ConsoleLog::GetSingleton()->Print("  Removing non-lootable: %s (x%d)", 
                                                  item->GetName(), count);
            lootData.removedItems.push_back({item, count});
            a_actor->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
        }
    }
}

void LootManager::RestoreNonLootableItems(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    std::lock_guard<std::mutex> lock(dataMutex);
    
    auto it = actorLootData.find(a_actor->GetFormID());
    if (it == actorLootData.end()) {
        return;
    }
    
    auto& lootData = it->second;
    
    RE::ConsoleLog::GetSingleton()->Print("Container closed for actor %X, restoring %zu items", 
                                          a_actor->GetFormID(), lootData.removedItems.size());
    
    for (const auto& [item, count] : lootData.removedItems) {
        a_actor->AddObjectToContainer(item, nullptr, count, nullptr);
    }
    
    lootData.removedItems.clear();
}

bool LootManager::ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor) {
    if (!a_item) return true;
    
    if (a_item->IsGold()) {
        return true;
    }
    
    if (a_item->GetFormType() == RE::FormType::Misc) {
        if (auto* miscItem = a_item->As<RE::TESObjectMISC>()) {
            if (miscItem && miscItem->GetFullName()) {
                std::string_view name(miscItem->GetFullName());
                if (name.find("Key") != std::string_view::npos) {
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
            }
            else if (base->GetRace() && base->GetRace()->formEditorID.contains("Bandit")) {
                multiplier *= settings->banditMultiplier;
            }
        }
    }
    
    return std::min(1.0f, baseChance * multiplier);
}