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

void LootManager::ProcessActorDeath(RE::Actor* a_actor, RE::Actor* /*a_killer*/) {
    auto actorHandle = a_actor->GetHandle();
    
    std::thread([this, actorHandle]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        auto* actor = actorHandle.get().get();
        if (!actor) return;
        
        std::lock_guard<std::mutex> lock(processingMutex);
        
        auto droppedItems = RollForLoot(actor);
        DropLootPhysically(actor, droppedItems);
        CleanInventory(actor, droppedItems);
        actor->SetActivationBlocked(true);
        
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

std::vector<LootManager::DroppedItem> LootManager::RollForLoot(RE::Actor* a_actor) {
    std::vector<DroppedItem> result;
    
    auto inventory = a_actor->GetInventory();
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) continue;
        
        // Skip gold - handle separately
        if (item->IsGold()) continue;
        
        // Essential items always drop
        if (IsEssentialItem(item)) {
            result.push_back({item, count});
            continue;
        }
        
        // Roll for each item
        std::int32_t dropCount = 0;
        for (std::int32_t i = 0; i < count; ++i) {
            if (ShouldDropItem(item, a_actor)) {
                dropCount++;
            }
        }
        
        if (dropCount > 0) {
            result.push_back({item, dropCount});
        }
    }
    
    return result;
}

void LootManager::DropLootPhysically(RE::Actor* a_actor, const std::vector<DroppedItem>& items) {
    if (items.empty()) return;
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> angleDist(0.0f, 360.0f);
    std::uniform_real_distribution<float> radiusDist(50.0f, 120.0f);
    
    auto corpsePos = a_actor->GetPosition();
    
    for (const auto& [item, count] : items) {
        float angle = angleDist(gen) * (3.14159f / 180.0f);
        float radius = radiusDist(gen);
        
        float offsetX = radius * std::cos(angle);
        float offsetY = radius * std::sin(angle);
        
        auto droppedHandle = a_actor->DropObject(item, nullptr, count);
        if (auto dropped = droppedHandle.get(); dropped) {
            RE::NiPoint3 dropPos;
            dropPos.x = corpsePos.x + offsetX;
            dropPos.y = corpsePos.y + offsetY;
            dropPos.z = corpsePos.z + 50.0f;
            
            dropped->SetPosition(dropPos);
        }
    }
}

void LootManager::CleanInventory(RE::Actor* a_actor, const std::vector<DroppedItem>& droppedItems) {
    auto inventory = a_actor->GetInventory();
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) continue;
        
        // Keep gold in corpse inventory
        if (item->IsGold()) continue;
        
        // Check if this item was dropped
        bool wasDropped = false;
        std::int32_t droppedCount = 0;
        
        for (const auto& dropped : droppedItems) {
            if (dropped.item == item) {
                wasDropped = true;
                droppedCount = dropped.count;
                break;
            }
        }
        
        // Remove items that weren't dropped
        if (wasDropped) {
            // Remove the count that was dropped, keep the rest
            std::int32_t remaining = count - droppedCount;
            if (remaining > 0) {
                a_actor->RemoveItem(item, remaining, RE::ITEM_REMOVE_REASON::kRemove, 
                    nullptr, nullptr);
            }
        } else {
            // Remove all of this item
            a_actor->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kRemove, 
                nullptr, nullptr);
        }
    }
}

bool LootManager::IsEssentialItem(RE::TESBoundObject* a_item) {
    if (!a_item) return false;
    
    // Quest items
    if (a_item->GetFormType() == RE::FormType::KeyMaster) {
        return true;
    }
    
    // Check for quest item flag in misc items
    if (auto* misc = a_item->As<RE::TESObjectMISC>()) {
        auto* fullName = misc->GetFullName();
        if (fullName && std::string_view(fullName).find("Key") != std::string_view::npos) {
            return true;
        }
    }
    
    return false;
}

bool LootManager::ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor) {
    if (!a_item) return false;
    
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