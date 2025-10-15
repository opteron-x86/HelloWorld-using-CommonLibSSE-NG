#include "LootManager.h"
#include "Settings.h"
#include <random>

void LootManager::Register() {
    auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (eventSource) {
        eventSource->AddEventSink<RE::TESDeathEvent>(this);
    }
    
    auto* ui = RE::UI::GetSingleton();
    if (ui) {
        ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
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
    const RE::MenuOpenCloseEvent* a_event,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
    
    if (!a_event) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    // Container menu opened
    if (a_event->menuName == "ContainerMenu" && a_event->opening) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return RE::BSEventNotifyControl::kContinue;
        }
        
        auto target = player->GetGrabbedRef();
        if (!target) {
            target = RE::Offset::CrosshairPickData::GetSingleton()->target.get().get();
        }
        
        auto* actor = target ? target->As<RE::Actor>() : nullptr;
        if (actor && actor->IsDead()) {
            currentLootTarget = actor;
        }
    }
    
    // Container menu closed - restore items
    if (a_event->menuName == "ContainerMenu" && !a_event->opening && currentLootTarget) {
        RestoreNonLootableItems(currentLootTarget);
        currentLootTarget = nullptr;
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

void LootManager::ProcessActorDeath(RE::Actor* a_actor, RE::Actor* a_killer) {
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
    
    if (a_actor->IsEssential()) {
        return false;
    }
    
    auto* base = a_actor->GetActorBase();
    if (base && base->IsSummonable()) {
        return false;
    }
    
    return true;
}

RE::TESObjectREFR* LootManager::GetOrCreateTempStorage() {
    if (tempStorage) {
        return tempStorage;
    }
    
    // Create hidden container far below world
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return nullptr;
    }
    
    auto* containerBase = RE::TESForm::LookupByID<RE::TESObjectCONT>(0x0010E1A7); // WEBarrelFood01
    if (!containerBase) {
        return nullptr;
    }
    
    auto* cell = player->GetParentCell();
    if (!cell) {
        return nullptr;
    }
    
    tempStorage = cell->PlaceObjectAtMe(containerBase, false).get();
    if (tempStorage) {
        tempStorage->SetPosition(0, 0, -10000);
        tempStorage->data.objectReference->SetActorCause(nullptr);
    }
    
    return tempStorage;
}

void LootManager::FilterInventory(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    auto inventory = a_actor->GetInventory();
    StoredItems stored;
    auto* storage = GetOrCreateTempStorage();
    
    if (!storage) {
        return;
    }
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) {
            continue;
        }
        
        // Always allow gold and quest items
        if (item->IsGold()) {
            continue;
        }
        
        // Check for keys (always lootable)
        if (item->GetFormType() == RE::FormType::Misc) {
            auto* miscItem = item->As<RE::TESObjectMISC>();
            if (miscItem && miscItem->GetFullName()) {
                std::string_view name(miscItem->GetFullName());
                if (name.find("Key") != std::string_view::npos) {
                    continue;
                }
            }
        }
        
        // Track if item is equipped
        bool isEquipped = entry->IsWorn();
        
        // Determine if item should be lootable
        std::int32_t lootableCount = 0;
        for (std::int32_t i = 0; i < count; ++i) {
            if (ShouldDropItem(item, a_actor)) {
                lootableCount++;
            }
        }
        
        // Store non-lootable items
        std::int32_t toStore = count - lootableCount;
        if (toStore > 0) {
            a_actor->RemoveItem(item, toStore, RE::ITEM_REMOVE_REASON::kRemove, 
                               nullptr, storage);
            stored.items.push_back({item, toStore});
            
            if (isEquipped) {
                stored.equippedItems.insert(item);
            }
        }
    }
    
    if (!stored.items.empty()) {
        actorStoredItems[a_actor->GetFormID()] = std::move(stored);
    }
}

void LootManager::RestoreNonLootableItems(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    auto it = actorStoredItems.find(a_actor->GetFormID());
    if (it == actorStoredItems.end()) {
        return;
    }
    
    auto* storage = GetOrCreateTempStorage();
    if (!storage) {
        return;
    }
    
    auto& stored = it->second;
    
    // Restore items from storage
    for (const auto& [item, count] : stored.items) {
        storage->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kRemove, 
                           nullptr, a_actor);
        
        // Re-equip if it was originally equipped
        if (stored.equippedItems.find(item) != stored.equippedItems.end()) {
            a_actor->EquipObject(item);
        }
    }
    
    actorStoredItems.erase(it);
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