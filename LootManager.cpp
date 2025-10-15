// LootManager.cpp
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
    
    if (actor && ShouldProcessActor(actor)) {
        ProcessActorDeath(actor);
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

void LootManager::ProcessActorDeath(RE::Actor* a_actor) {
    auto actorHandle = a_actor->GetHandle();
    
    std::thread([this, actorHandle]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto* actor = actorHandle.get().get();
        
        if (actor) {
            std::lock_guard<std::mutex> lock(processingMutex);
            ProcessInventory(actor);
            
            // Block activation so player can't open inventory directly
            actor->GetActorRuntimeData().boolFlags.set(
                RE::Actor::BOOL_FLAGS::kDontShowOnStealthMeter);
            actor->AddToFaction(RE::TESForm::LookupByID<RE::TESFaction>(0x0005C84E), 0); // Prevent activation
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

bool LootManager::IsEquipped(RE::Actor* a_actor, RE::TESBoundObject* a_item) {
    auto* equipped = a_actor->GetEquippedObject(false);
    if (equipped == a_item) return true;
    
    equipped = a_actor->GetEquippedObject(true);
    if (equipped == a_item) return true;
    
    // Check armor slots
    if (auto* armor = a_item->As<RE::TESObjectARMO>()) {
        auto slotMask = armor->GetSlotMask();
        auto* worn = a_actor->GetWornArmor(slotMask);
        return worn == armor;
    }
    
    return false;
}

void LootManager::ProcessInventory(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    auto inventory = a_actor->GetInventory();
    RE::NiPoint3 dropLocation = a_actor->GetPosition();
    dropLocation.z += 50.0f; // Slight elevation for visibility
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) continue;
        
        bool isGold = item->IsGold();
        bool isEquipped = IsEquipped(a_actor, item);
        bool shouldDrop = isGold || ShouldDropItem(item, a_actor);
        
        if (shouldDrop) {
            // Drop items on ground
            for (std::int32_t i = 0; i < count; ++i) {
                if (auto* dropped = a_actor->DropObject(item, nullptr, 1)) {
                    dropped->MoveTo(a_actor); // Ensure near corpse
                }
            }
        } else if (!isEquipped) {
            // Remove non-equipped, non-droppable items
            a_actor->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
        }
        // Equipped items that don't drop stay on corpse for visuals
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
        if (a_actor->GetLevel() > 30) {
            multiplier *= settings->bossMultiplier;
        }
    }
    
    return std::min(1.0f, baseChance * multiplier);
}