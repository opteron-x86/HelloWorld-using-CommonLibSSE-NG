#include "LootManager.h"
#include "Settings.h"
#include <random>

void LootManager::Register() {
    auto* scriptEventSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (scriptEventSource) {
        scriptEventSource->AddEventSink(static_cast<RE::BSTEventSink<RE::TESDeathEvent>*>(this));
        scriptEventSource->AddEventSink(static_cast<RE::BSTEventSink<RE::TESActivateEvent>*>(this));
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
    const RE::TESActivateEvent* a_event,
    RE::BSTEventSource<RE::TESActivateEvent>*) {
    
    if (!enabled || !a_event) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    auto* targetRef = a_event->objectActivated.get();
    auto* activator = a_event->actionRef.get();
    
    if (!targetRef || !activator) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    auto* corpse = targetRef->As<RE::Actor>();
    if (corpse && corpse->IsDead()) {
        std::lock_guard<std::mutex> lock(mapMutex);
        auto it = rolledLootMap.find(corpse->GetFormID());
        
        if (it != rolledLootMap.end() && it->second.processed && !it->second.looted) {
            HandleCorpseActivation(corpse, activator);
            return RE::BSEventNotifyControl::kStop;
        }
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

void LootManager::ProcessActorDeath(RE::Actor* a_actor, RE::Actor* a_killer) {
    auto actorHandle = a_actor->GetHandle();
    
    std::thread([this, actorHandle]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto* actor = actorHandle.get().get();
        if (actor) {
            RollLootForActor(actor);
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

void LootManager::RollLootForActor(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    std::lock_guard<std::mutex> lock(mapMutex);
    
    auto formID = a_actor->GetFormID();
    auto& lootData = rolledLootMap[formID];
    
    if (lootData.processed) {
        return;
    }
    
    auto inventory = a_actor->GetInventory();
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) continue;
        
        // Gold always drops
        if (item->IsGold()) {
            lootData.items.push_back({item, count});
            continue;
        }
        
        // Quest items always drop
        if (item->GetFormType() == RE::FormType::Misc) {
            auto* miscItem = item->As<RE::TESObjectMISC>();
            if (miscItem && miscItem->GetFullName()) {
                std::string_view name(miscItem->GetFullName());
                if (name.find("Key") != std::string_view::npos) {
                    lootData.items.push_back({item, count});
                    continue;
                }
            }
        }
        
        // Roll for each item based on drop chance
        std::int32_t droppedCount = 0;
        
        if (count == 1) {
            if (ShouldDropItem(item, a_actor)) {
                droppedCount = 1;
            }
        } else {
            // For stacks, roll individually
            std::random_device rd;
            std::mt19937 gen(rd());
            
            for (std::int32_t i = 0; i < count; ++i) {
                if (ShouldDropItem(item, a_actor)) {
                    droppedCount++;
                }
            }
        }
        
        if (droppedCount > 0) {
            lootData.items.push_back({item, droppedCount});
        }
    }
    
    lootData.processed = true;
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

void LootManager::HandleCorpseActivation(RE::Actor* a_corpse, RE::TESObjectREFR* a_activator) {
    auto* container = CreateTempContainer(a_corpse);
    if (!container) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mapMutex);
    auto it = rolledLootMap.find(a_corpse->GetFormID());
    
    if (it != rolledLootMap.end()) {
        TransferLootToContainer(a_corpse, container, it->second);
        it->second.looted = true;
    }
    
    // Open container for player
    if (a_activator && a_activator->As<RE::Actor>()) {
        auto* player = a_activator->As<RE::Actor>();
        if (player && player->IsPlayerRef()) {
            RE::ActorEquipManager::GetSingleton()->OpenContainer(container);
            
            // Schedule cleanup
            std::thread([this, container]() {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                CleanupContainer(container);
            }).detach();
        }
    }
}

RE::TESObjectREFR* LootManager::CreateTempContainer(RE::Actor* a_corpse) {
    // Use a standard chest container
    auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESObjectCONT>();
    if (!factory) {
        return nullptr;
    }
    
    // Get a basic chest container from game data
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        return nullptr;
    }
    
    // Use a common chest container (ChestSmall is 0x0001A2AD)
    auto* chestForm = dataHandler->LookupForm<RE::TESObjectCONT>(0x0001A2AD, "Skyrim.esm");
    if (!chestForm) {
        return nullptr;
    }
    
    // Place container near corpse (invisible)
    auto* container = a_corpse->PlaceObjectAtMe(chestForm, false);
    if (container) {
        container->SetPosition(a_corpse->GetPosition());
        container->data.angle = a_corpse->data.angle;
        
        // Make it invisible and disable collision
        container->Get3D()->SetVisible(false);
    }
    
    return container.get();
}

void LootManager::TransferLootToContainer(RE::Actor* a_corpse, RE::TESObjectREFR* a_container, const RolledLoot& a_loot) {
    for (const auto& [item, count] : a_loot.items) {
        if (item && count > 0) {
            a_corpse->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, a_container);
        }
    }
}

void LootManager::CleanupContainer(RE::TESObjectREFR* a_container) {
    if (a_container) {
        a_container->SetDelete(true);
    }
}