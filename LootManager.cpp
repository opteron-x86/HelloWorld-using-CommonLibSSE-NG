#include "LootManager.h"
#include "Settings.h"
#include <random>

void LootManager::Register() {
    auto* scriptEventSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (scriptEventSource) {
        scriptEventSource->AddEventSink<RE::TESDeathEvent>(this);
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
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        auto* actor = actorHandle.get().get();
        if (actor) {
            std::lock_guard<std::mutex> lock(processingMutex);
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
    std::unordered_set<RE::FormID> lootableItems;
    
    RE::ConsoleLog::GetSingleton()->Print("Processing actor: %X with %zu items", 
                                          a_actor->GetFormID(), inventory.size());
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) {
            continue;
        }
        
        if (item->IsGold()) {
            lootableItems.insert(item->GetFormID());
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
            lootableItems.insert(item->GetFormID());
        }
        
        RE::ConsoleLog::GetSingleton()->Print("  Item %X (%s): %s", 
                                              item->GetFormID(),
                                              item->GetName(),
                                              shouldDrop ? "LOOTABLE" : "BLOCKED");
    }
    
    RE::ConsoleLog::GetSingleton()->Print("Determined %zu lootable items, filtering inventory", 
                                          lootableItems.size());
    
    FilterCorpseInventory(a_actor, lootableItems);
}

void LootManager::FilterCorpseInventory(RE::Actor* a_actor, const std::unordered_set<RE::FormID>& lootableItems) {
    if (!a_actor) return;
    
    auto inventory = a_actor->GetInventory();
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) {
            continue;
        }
        
        if (item->IsGold()) {
            continue;
        }
        
        if (lootableItems.contains(item->GetFormID())) {
            continue;
        }
        
        if (IsItemEquipped(a_actor, item)) {
            RE::ConsoleLog::GetSingleton()->Print("  Keeping equipped: %s", item->GetName());
            continue;
        }
        
        RE::ConsoleLog::GetSingleton()->Print("  Removing: %s (x%d)", item->GetName(), count);
        a_actor->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
    }
}

bool LootManager::IsItemEquipped(RE::Actor* a_actor, RE::TESBoundObject* a_item) {
    if (!a_actor || !a_item) {
        return false;
    }
    
    auto inventory = a_actor->GetInventory();
    auto it = inventory.find(a_item);
    
    if (it == inventory.end()) {
        return false;
    }
    
    auto& [count, entry] = it->second;
    if (!entry || !entry->extraLists) {
        return false;
    }
    
    for (auto* xList : *entry->extraLists) {
        if (xList && xList->HasType(RE::ExtraDataType::kWorn) || 
            xList && xList->HasType(RE::ExtraDataType::kWornLeft)) {
            return true;
        }
    }
    
    return false;
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