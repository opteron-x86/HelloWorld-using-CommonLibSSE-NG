#include "LootManager.h"
#include "Settings.h"
#include "ContainerMenuHook.h"
#include <random>
#include <chrono>

void LootManager::Register() {
    auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (eventSource) {
        eventSource->AddEventSink<RE::TESDeathEvent>(this);
        eventSource->AddEventSink<RE::TESContainerChangedEvent>(this);
    }
    
    // Install container menu hooks
    ContainerMenuHook::Install();
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
    const RE::TESContainerChangedEvent* a_event,
    RE::BSTEventSource<RE::TESContainerChangedEvent>*) {
    
    if (!a_event || !a_event->newContainer || a_event->oldContainer != a_event->reference) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    // Player is looting
    if (a_event->newContainer == 0x14) { // Player FormID
        auto* container = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_event->oldContainer);
        auto* item = RE::TESForm::LookupByID<RE::TESBoundObject>(a_event->baseObj);
        
        if (container && item) {
            std::lock_guard<std::mutex> lock(mapMutex);
            
            auto it = actorLootMap.find(container->GetFormID());
            if (it != actorLootMap.end()) {
                // Check if item is lootable
                if (it->second.items.find(item->GetFormID()) == it->second.items.end()) {
                    // Item not in lootable list - return it to container
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    if (player) {
                        player->RemoveItem(item, a_event->itemCount, 
                            RE::ITEM_REMOVE_REASON::kRemove, nullptr, container);
                    }
                }
            }
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
            MarkUnlootableItems(actor);
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

void LootManager::MarkUnlootableItems(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    std::lock_guard<std::mutex> lock(mapMutex);
    
    // Clean up old entries (older than 1 hour)
    auto now = std::chrono::steady_clock::now();
    for (auto it = actorLootMap.begin(); it != actorLootMap.end();) {
        if (std::chrono::duration_cast<std::chrono::hours>(now - it->second.timestamp).count() > 1) {
            it = actorLootMap.erase(it);
        } else {
            ++it;
        }
    }
    
    // Build lootable items list
    LootableItems lootableItems;
    lootableItems.timestamp = now;
    
    auto inventory = a_actor->GetInventory();
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (item && count > 0) {
            // Check each item individually for drop chance
            if (ShouldDropItem(item, a_actor)) {
                lootableItems.items.insert(item->GetFormID());
            }
        }
    }
    
    // Add equipped items to lootable list based on drop chance
    auto processEquipped = [&](RE::BIPED_MODEL::BipedObjectSlot slot) {
        auto* equipped = a_actor->GetWornArmor(slot);
        if (equipped && ShouldDropItem(equipped, a_actor)) {
            lootableItems.items.insert(equipped->GetFormID());
        }
    };
    
    // Process all equipment slots
    for (int i = 0; i < 32; ++i) {
        processEquipped(static_cast<RE::BIPED_MODEL::BipedObjectSlot>(1 << i));
    }
    
    // Process weapons
    if (auto* rightHand = a_actor->GetEquippedObject(false)) {
        if (auto* weapon = rightHand->As<RE::TESObjectWEAP>()) {
            if (ShouldDropItem(weapon, a_actor)) {
                lootableItems.items.insert(weapon->GetFormID());
            }
        }
    }
    
    if (auto* leftHand = a_actor->GetEquippedObject(true)) {
        if (auto* weapon = leftHand->As<RE::TESObjectWEAP>()) {
            if (ShouldDropItem(weapon, a_actor)) {
                lootableItems.items.insert(weapon->GetFormID());
            }
        }
    }
    
    actorLootMap[a_actor->GetFormID()] = std::move(lootableItems);
}

bool LootManager::IsItemLootable(RE::FormID a_containerID, RE::TESBoundObject* a_item) {
    if (!a_item) return true;
    
    // Always allow gold and quest items
    if (a_item->IsGold()) {
        return true;
    }
    
    std::lock_guard<std::mutex> lock(mapMutex);
    
    auto it = actorLootMap.find(a_containerID);
    if (it != actorLootMap.end()) {
        return it->second.items.find(a_item->GetFormID()) != it->second.items.end();
    }
    
    // If not in our map, allow looting (for compatibility)
    return true;
}

bool LootManager::ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor) {
    if (!a_item) return true;
    
    // Always drop gold
    if (a_item->IsGold()) {
        return true;
    }
    
    // Always drop keys and quest items
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
    
    // Check if enchanted
    if (auto* enchantable = a_item->As<RE::TESEnchantableForm>()) {
        if (enchantable->formEnchanting) {
            multiplier *= settings->enchantedMultiplier;
        }
    }
    
    // Check if unique/legendary
    if (a_item->GetGoldValue() > 1000) {
        multiplier *= settings->uniqueMultiplier;
    }
    
    // Apply NPC type modifiers
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