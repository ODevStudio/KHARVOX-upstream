    if(submitResult!=VK_SUCCESS){
        s.fsr1.discardRecordedFrame();
        // Recording already advanced AER cache revisions, source identities
        // and cached eye poses. A failed submit executes none of those copies,
        // so discard the host-side claims just like a recording failure.
        invalidateAlternatingStereoHistory(true);
        if(freshAerHands)s.freshHandsWorldValid=false;
        if(nativeFrameValid)kharvox::native::fail("owner native copy submission failed; restart required");
        s.handRenderer.finishSceneIntegratedFrame();
        releaseAcquiredEyeImages();
        if(s.hudImageState.owned())releaseImageChecked(s.hudQuad.handle,s.hudImageState);
        log("copy submit failed "+std::to_string(submitResult));endEmptyFrame("copy-submit-failed");return;
    }
