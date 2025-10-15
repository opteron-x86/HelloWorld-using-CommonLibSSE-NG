#include "LootManager.h"
#include "Settings.h"
#include <random>

void LootManager::Register() {
    auto* deathSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (deathSource) {
        deathSource->AddEventSink<RE::TESDeathEvent>(this);
        deathSource->AddEventSink<RE::TESContainerChangedEvent>(this);
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
    
    if (!a_event || !a_event->oldContainer || a_event->newContainer != 0x14) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    auto* form = RE::TESForm::LookupByID(a_event->oldContainer);
    if (!form) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    auto* actor = form->As<RE::Actor>();
    if (!actor || !actor->IsDead()) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    auto* item = RE::TESForm::LookupByID(a_event->baseObj);
    auto* boundObj = item ? item->As<RE::TESBoundObject>() : nullptr;
    
    if (!boundObj) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    // Check if item is lootable
    if (!IsItemLootable(actor, boundObj)) {
        // Return item to corpse immediately
        SKSE::GetTaskInterface()->AddTask([actor, boundObj, count = a_event->itemCount]() {
            if (actor) {
                actor->AddObjectToContainer(boundObj, nullptr, count, nullptr);
            }
        });
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

void LootManager::ProcessActorDeath(RE::Actor* a_actor, RE::Actor* a_killer) {
    if (!a_actor) return;
    
    // Clean old entries periodically
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(lootMapMutex);
        std::erase_if(actorLootMap, [&](const auto& pair) {
            return std::chrono::duration_cast<std::chrono::hours>(
                now - pair.second.timestamp).count() > 24;
        });
    }
    
    DetermineLootableItems(a_actor);
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
    LootData lootData;
    lootData.timestamp = std::chrono::steady_clock::now();
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) continue;
        
        // Gold always drops
        if (item->IsGold()) {
            lootData.lootableItems.insert(item->GetFormID());
            continue;
        }
        
        // Quest items always drop
        if (item->HasKeyword(0x0002A8E8)) { // QuestItem keyword
            lootData.lootableItems.insert(item->GetFormID());
            continue;
        }
        
        // Keys always drop
        if (item->IsKey()) {
            lootData.lootableItems.insert(item->GetFormID());
            continue;
        }
        
        // Roll for each stack
        bool anyDropped = false;
        for (std::int32_t i = 0; i < count; ++i) {
            if (ShouldDropItem(item, a_actor)) {
                anyDropped = true;
                break;
            }
        }
        
        if (anyDropped) {
            lootData.lootableItems.insert(item->GetFormID());
        }
    }
    
    // Store loot data
    {
        std::lock_guard<std::mutex> lock(lootMapMutex);
        actorLootMap[a_actor->GetFormID()] = std::move(lootData);
    }
}

bool LootManager::IsItemLootable(RE::Actor* a_actor, RE::TESBoundObject* a_item) {
    if (!a_actor || !a_item) return true;
    
    // Always allow gold
    if (a_item->IsGold()) return true;
    
    // Always allow quest items
    if (a_item->HasKeyword(0x0002A8E8)) return true;
    
    // Always allow keys
    if (a_item->IsKey()) return true;
    
    std::lock_guard<std::mutex> lock(lootMapMutex);
    auto it = actorLootMap.find(a_actor->GetFormID());
    if (it == actorLootMap.end()) {
        // No loot data means actor death wasn't processed, allow all
        return true;
    }
    
    return it->second.lootableItems.contains(a_item->GetFormID());
}

bool LootManager::ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor) {
    if (!a_item) return true;
    
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