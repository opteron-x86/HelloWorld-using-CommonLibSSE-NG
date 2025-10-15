// LootManager.h
#pragma once

class LootManager : public RE::BSTEventSink<RE::TESDeathEvent> {
public:
    static LootManager* GetSingleton() {
        static LootManager singleton;
        return &singleton;
    }
    
    void Register();
    
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESDeathEvent* a_event,
        RE::BSTEventSource<RE::TESDeathEvent>* a_eventSource) override;

private:
    LootManager() = default;
    ~LootManager() = default;
    
    LootManager(const LootManager&) = delete;
    LootManager(LootManager&&) = delete;
    LootManager& operator=(const LootManager&) = delete;
    LootManager& operator=(LootManager&&) = delete;
    
    void ProcessActorDeath(RE::Actor* a_actor);
    bool ShouldProcessActor(RE::Actor* a_actor);
    void ProcessInventory(RE::Actor* a_actor);
    bool ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor);
    bool IsEquipped(RE::Actor* a_actor, RE::TESBoundObject* a_item);
    float GetDropChance(RE::TESBoundObject* a_item, RE::Actor* a_actor);
    
    std::atomic<bool> enabled{true};
    std::mutex processingMutex;
};