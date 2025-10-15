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
    auto* killer = a_event->actorKiller ? a_event->actorKiller->As<RE::Actor>() : nullptr;
    
    if (actor && ShouldProcessActor(actor)) {
        ProcessActorDeath(actor, killer);
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

void LootManager::ProcessActorDeath(RE::Actor* a_actor, RE::Actor* /*a_killer*/) {
    auto actorHandle = a_actor->GetHandle();
    
    std::thread([this, actorHandle]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        auto* actor = actorHandle.get().get();
        if (actor) {
            std::lock_guard<std::mutex> lock(processingMutex);
            ProcessLoot(actor);
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

RE::TESObjectREFR* LootManager::CreateLootContainer(RE::Actor* a_actor) {
    // Use satchel container
    auto* containerBase = RE::TESForm::LookupByID<RE::TESObjectCONT>(0x000E6C43);
    if (!containerBase) {
        return nullptr;
    }
    
    auto* cell = a_actor->GetParentCell();
    if (!cell) {
        return nullptr;
    }
    
    // Place container at actor's feet
    RE::NiPoint3 pos = a_actor->GetPosition();
    pos.z += 10.0f;
    
    auto containerPtr = a_actor->PlaceObjectAtMe(containerBase, false);
    if (!containerPtr) {
        return nullptr;
    }
    
    auto* container = containerPtr.get();
    if (container) {
        container->SetPosition(pos);
    }
    
    return container;
}

void LootManager::ProcessLoot(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    auto inventory = a_actor->GetInventory();
    std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> itemsToDrop;
    
    // Evaluate all items
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) {
            continue;
        }
        
        // Gold always transfers
        if (item->IsGold()) {
            itemsToDrop.push_back({item, count});
            continue;
        }
        
        // Keys always transfer
        if (auto* key = item->As<RE::TESKey>()) {
            itemsToDrop.push_back({item, count});
            continue;
        }
        
        // Skip equipped items (stays on corpse for visuals)
        if (entry && entry->IsWorn()) {
            continue;
        }
        
        // Roll for non-equipped items
        std::int32_t dropCount = 0;
        for (std::int32_t i = 0; i < count; ++i) {
            if (ShouldDropItem(item, a_actor)) {
                dropCount++;
            }
        }
        
        if (dropCount > 0) {
            itemsToDrop.push_back({item, dropCount});
        }
    }
    
    // Create loot container
    auto* container = CreateLootContainer(a_actor);
    if (!container) {
        return;
    }
    
    // Transfer items to container
    for (const auto& [item, count] : itemsToDrop) {
        a_actor->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, container);
    }
    
    // Block corpse activation
    a_actor->SetActivationBlocked(true);
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