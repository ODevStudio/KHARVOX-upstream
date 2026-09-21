void KharvoxXRInstallGameHooks(){
    static std::once_flag installation;
    std::call_once(installation,[]{
        std::lock_guard<std::mutex> lock(mutex);
        auto& hold=s.cinematicAdaptiveTick;
        hold.layoutChecked=true;
        auto image=const_cast<unsigned char*>(kharvox::gameMemory::mainImage().base);
        hold.layoutAvailable=kharvox::gameMemory::supportedDoomImage()
            &&validateDoomAdaptiveTickLayout(image)&&installImmersiveAdaptiveParticipantBypass(image);
        if(!hold.layoutAvailable)log("[CINEMATIC-HZ] startup game layout unavailable; refresh hold disabled");
    });
}
