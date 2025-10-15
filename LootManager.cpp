#include "LootManager.h"
#include "Settings.h"
#include <random>

void LootManager::Register() {
    auto* scriptEventSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (scriptEventSource) {
        scriptEventSource->AddEventSink<RE::TESDeathEvent>(this);
        scriptEventSource->AddEventSink<RE::TESContainerChangedEvent>(this);
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
    
    if (!a_event || a_event->newContainer != 0x14) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || a_event->newContainer != player->GetFormID()) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    auto* oldContainer = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_event->oldContainer);
    auto* item = RE::TESForm::LookupByID<RE::TESBoundObject>(a_event->baseObj);
    
    if (!oldContainer || !item) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    if (!IsItemLootable(oldContainer, item)) {
        player->RemoveItem(item, a_event->itemCount, RE::ITEM_REMOVE_REASON::kStoreInContainer, 
                          nullptr, oldContainer);
        
        RE::DebugNotification("You cannot take that item.");
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

bool LootManager::IsItemLootable(RE::TESObjectREFR* a_container, RE::TESBoundObject* a_item) {
    if (!a_container || !a_item) {
        return true;
    }
    
    if (a_item->IsGold()) {
        return true;
    }
    
    std::lock_guard<std::mutex> lock(dataMutex);
    
    auto it = actorLootData.find(a_container->GetFormID());
    if (it == actorLootData.end()) {
        return true;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - it->second.timestamp);
    
    if (elapsed.count() > 10) {
        actorLootData.erase(it);
        return true;
    }
    
    return it->second.lootableItems.contains(a_item->GetFormID());
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
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) {
            continue;
        }
        
        if (item->IsGold()) {
            lootData.lootableItems.insert(item->GetFormID());
            continue;
        }
        
        if (count > 1) {
            std::random_device rd;
            std::mt19937 gen(rd());
            
            for (std::int32_t i = 0; i < count; ++i) {
                if (ShouldDropItem(item, a_actor)) {
                    lootData.lootableItems.insert(item->GetFormID());
                    break;
                }
            }
        } else {
            if (ShouldDropItem(item, a_actor)) {
                lootData.lootableItems.insert(item->GetFormID());
            }
        }
    }
    
    std::lock_guard<std::mutex> lock(dataMutex);
    actorLootData[a_actor->GetFormID()] = std::move(lootData);
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