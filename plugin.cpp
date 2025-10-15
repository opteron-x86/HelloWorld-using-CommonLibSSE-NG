#include "LootManager.h"
#include "Settings.h"
#include "Papyrus.h"

namespace {
    void MessageHandler(SKSE::MessagingInterface::Message* a_msg) {
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            LootManager::GetSingleton()->Register();
            Settings::GetSingleton()->Load();
            RE::ConsoleLog::GetSingleton()->Print("Loot Drop System initialized");
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse);
    
    Papyrus::Register();
    SKSE::GetMessagingInterface()->RegisterListener(MessageHandler);
    
    return true;
}