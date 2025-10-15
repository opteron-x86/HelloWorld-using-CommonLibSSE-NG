#include "LootManager.h"
#include "Settings.h"
#include <random>

void LootManager::Register() {
    auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (eventSource) {
        eventSource->AddEventSink<RE::TESDeathEvent>(this);
        eventSource->AddEventSink<RE::TESActivateEvent>(this);
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
    
    if (actor && ShouldProcessActor(actor)) {
        ProcessActorDeath(actor);
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl LootManager::ProcessEvent(
    const RE::TESActivateEvent* a_event,
    RE::BSTEventSource<RE::TESActivateEvent>*) {
    
    if (!enabled || !a_event || !a_event->objectActivated || !a_event->actionRef) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    auto* activated = a_event->objectActivated->As<RE::Actor>();
    auto* activator = a_event->actionRef->As<RE::Actor>();
    
    if (activated && activator && activated->IsDead() && activator->IsPlayerRef()) {
        std::lock_guard<std::mutex> lock(dataMutex);
        auto it = lootDataMap.find(activated->GetFormID());
        if (it != lootDataMap.end()) {
            auto corpseHandle = activated->GetHandle();
            auto activatorHandle = activator->GetHandle();
            
            SKSE::GetTaskInterface()->AddTask([this, corpseHandle, activatorHandle]() {
                auto corpse = corpseHandle.get();
                auto activator = activatorHandle.get();
                if (corpse && activator) {
                    HandleCorpseActivation(corpse.get()->As<RE::Actor>(), activator.get()->As<RE::Actor>());
                }
            });
            
            return RE::BSEventNotifyControl::kStop;
        }
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl LootManager::ProcessEvent(
    const RE::MenuOpenCloseEvent* a_event,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
    
    if (!a_event || a_event->menuName != RE::ContainerMenu::MENU_NAME) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    if (!a_event->opening && activeCorpseID != 0) {
        auto corpseID = activeCorpseID;
        activeCorpseID = 0;
        
        SKSE::GetTaskInterface()->AddTask([this, corpseID]() {
            CleanupContainer(corpseID);
        });
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

void LootManager::ProcessActorDeath(RE::Actor* a_actor) {
    auto actorHandle = a_actor->GetHandle();
    
    std::thread([this, actorHandle]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        
        auto actor = actorHandle.get();
        if (!actor) return;
        
        auto* actorPtr = actor.get();
        if (actorPtr) {
            DetermineAllowedLoot(actorPtr);
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

void LootManager::DetermineAllowedLoot(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    std::lock_guard<std::mutex> lock(dataMutex);
    
    auto actorID = a_actor->GetFormID();
    LootData lootData;
    
    auto inventory = a_actor->GetInventory();
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) continue;
        
        // Gold always drops
        if (item->IsGold()) {
            lootData.allowedItems.push_back({item, count});
            continue;
        }
        
        // Roll for each item in stack
        std::int32_t allowedCount = 0;
        std::random_device rd;
        std::mt19937 gen(rd());
        
        for (std::int32_t i = 0; i < count; ++i) {
            if (ShouldDropItem(item, a_actor)) {
                allowedCount++;
            }
        }
        
        if (allowedCount > 0) {
            lootData.allowedItems.push_back({item, allowedCount});
        }
    }
    
    lootDataMap[actorID] = lootData;
}

void LootManager::HandleCorpseActivation(RE::Actor* a_corpse, RE::Actor* a_activator) {
    if (!a_corpse || !a_activator) return;
    
    std::lock_guard<std::mutex> lock(dataMutex);
    
    auto corpseID = a_corpse->GetFormID();
    auto it = lootDataMap.find(corpseID);
    if (it == lootDataMap.end()) return;
    
    auto& lootData = it->second;
    
    // If no items allowed, show message and return
    if (lootData.allowedItems.empty()) {
        RE::DebugNotification("Nothing to loot");
        return;
    }
    
    CreateLootContainer(a_corpse, a_activator);
}

void LootManager::CreateLootContainer(RE::Actor* a_corpse, RE::Actor* a_activator) {
    if (!a_corpse || !a_activator) return;
    
    auto corpseID = a_corpse->GetFormID();
    auto& lootData = lootDataMap[corpseID];
    
    // Check if container already exists
    if (lootData.hasContainer) {
        auto containerPtr = lootData.containerHandle.get();
        if (containerPtr) {
            containerPtr->Activate(a_activator, nullptr, 1, nullptr, 0);
            activeCorpseID = corpseID;
            return;
        }
    }
    
    // Create new container - using a chest from base game
    auto* containerBase = RE::TESForm::LookupByID<RE::TESObjectCONT>(0x00070008); // Chest01
    if (!containerBase) {
        RE::ConsoleLog::GetSingleton()->Print("LootDropSystem: Failed to find container base");
        return;
    }
    
    auto container = a_corpse->PlaceObjectAtMe(containerBase, false);
    if (!container) {
        RE::ConsoleLog::GetSingleton()->Print("LootDropSystem: Failed to create container");
        return;
    }
    
    // Make container invisible and position it at corpse
    container->SetPosition(a_corpse->GetPosition());
    container->data.angle = a_corpse->data.angle;
    
    // Transfer allowed items from corpse to container
    for (const auto& [item, count] : lootData.allowedItems) {
        if (item && count > 0) {
            a_corpse->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kStoreInContainer, 
                nullptr, container.get());
        }
    }
    
    lootData.containerHandle = container->GetHandle();
    lootData.hasContainer = true;
    
    // Open the container
    container->Activate(a_activator, nullptr, 1, nullptr, 0);
    activeCorpseID = corpseID;
}

void LootManager::CleanupContainer(RE::FormID a_corpseID) {
    std::lock_guard<std::mutex> lock(dataMutex);
    
    auto it = lootDataMap.find(a_corpseID);
    if (it == lootDataMap.end()) return;
    
    auto& lootData = it->second;
    
    if (!lootData.hasContainer) return;
    
    auto containerPtr = lootData.containerHandle.get();
    if (!containerPtr) {
        lootDataMap.erase(it);
        return;
    }
    
    // Return any remaining items to corpse
    auto* corpse = RE::TESForm::LookupByID<RE::Actor>(a_corpseID);
    if (corpse) {
        auto containerInv = containerPtr->GetInventory();
        for (const auto& [item, data] : containerInv) {
            auto& [count, entry] = data;
            if (item && count > 0) {
                containerPtr->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kStoreInContainer, 
                    nullptr, corpse);
            }
        }
    }
    
    // Delete container
    containerPtr->Disable();
    containerPtr->SetDelete(true);
    
    // Clean up loot data
    lootDataMap.erase(it);
}

bool LootManager::ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor) {
    if (!a_item) return false;
    
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