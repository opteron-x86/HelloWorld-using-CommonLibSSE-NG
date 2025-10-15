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
    auto actorRef = a_actor->GetActorBase();
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0 || item->IsGold()) {
            continue;
        }
        
        // Process each instance of this item type
        if (entry && entry->extraLists) {
            for (auto* extraList : *entry->extraLists) {
                if (extraList && !ShouldDropItem(item, a_actor)) {
                    // Mark as owned by the dead NPC - player can't loot it
                    if (!extraList->HasType(RE::ExtraDataType::kOwnership)) {
                        auto extraOwnership = RE::BSExtraData::Create<RE::ExtraOwnership>();
                        if (extraOwnership) {
                            extraOwnership->owner = actorRef;
                            extraList->Add(extraOwnership);
                        }
                    }
                }
            }
        }
        // Handle items without extra lists
        else if (!ShouldDropItem(item, a_actor)) {
            auto changes = a_actor->GetInventoryChanges();
            if (changes && changes->entryList) {
                for (auto& invEntry : *changes->entryList) {
                    if (invEntry && invEntry->object == item) {
                        if (!invEntry->extraLists) {
                            invEntry->extraLists = new std::remove_pointer_t<decltype(invEntry->extraLists)>();
                        }
                        
                        auto extraList = new RE::ExtraDataList();
                        auto extraOwnership = RE::BSExtraData::Create<RE::ExtraOwnership>();
                        if (extraOwnership && extraList) {
                            extraOwnership->owner = actorRef;
                            extraList->Add(extraOwnership);
                            invEntry->extraLists->push_back(extraList);
                        }
                    }
                }
            }
        }
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