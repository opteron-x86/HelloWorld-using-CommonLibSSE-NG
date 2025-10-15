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
    // Process in separate thread with proper lifetime management
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
    
    // Skip essential actors
    if (a_actor->IsEssential()) {
        return false;
    }
    
    // Skip summons and temporary actors
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
        
        if (item && count > 0 && !ShouldDropItem(item, a_actor)) {
            // Calculate how many to remove
            std::int32_t removeCount = 0;
            
            // For stackable items, randomly determine how many drop
            if (count > 1) {
                std::random_device rd;
                std::mt19937 gen(rd());
                
                for (std::int32_t i = 0; i < count; ++i) {
                    if (!ShouldDropItem(item, a_actor)) {
                        removeCount++;
                    }
                }
            } else {
                removeCount = 1;
            }
            
            if (removeCount > 0) {
                itemsToRemove.push_back({item, removeCount});
            }
        }
    }
    
    // Remove filtered items
    for (const auto& [item, count] : itemsToRemove) {
        a_actor->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
    }
}

bool LootManager::ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor) {
    if (!a_item) return true;
    
    // Always drop gold
    if (a_item->IsGold()) {
        return true;
    }
    
    // Check if it's a quest item by checking if it has the quest item flag
    // This requires checking the extra data in the inventory entry
    // For now, we'll be conservative and always drop potential quest items
    if (a_item->GetFormType() == RE::FormType::Misc) {
        // Check for known quest item FormIDs or keywords
        // For safety, always drop keys
        if (auto* miscItem = a_item->As<RE::TESObjectMISC>()) {
            if (miscItem && miscItem->GetFullName() && 
                std::string_view(miscItem->GetFullName()).find("Key") != std::string_view::npos) {
                return true;
            }
        }
    }
    
    // Check drop chance
    float dropChance = GetDropChance(a_item, a_actor);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    
    return dist(gen) < dropChance;
}

float LootManager::GetDropChance(RE::TESBoundObject* a_item, RE::Actor* a_actor) {
    auto* settings = Settings::GetSingleton();
    
    // Get base drop chance by item type
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
    
    // Apply quality modifiers
    float multiplier = 1.0f;
    
    // Check if enchanted
    if (auto* enchantable = a_item->As<RE::TESEnchantableForm>()) {
        if (enchantable->formEnchanting) {
            multiplier *= settings->enchantedMultiplier;
        }
    }
    
    // Check if unique/legendary (has a specific keyword or high value)
    if (a_item->GetGoldValue() > 1000) {
        multiplier *= settings->uniqueMultiplier;
    }
    
    // Apply NPC type modifiers
    if (a_actor) {
        auto* base = a_actor->GetActorBase();
        if (base) {
            // Boss check (level > 30 or has boss keyword)
            if (a_actor->GetLevel() > 30) {
                multiplier *= settings->bossMultiplier;
            }
            // Bandit check
            else if (base->GetRace() && base->GetRace()->formEditorID.contains("Bandit")) {
                multiplier *= settings->banditMultiplier;
            }
        }
    }
    
    return std::min(1.0f, baseChance * multiplier);
}