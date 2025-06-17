// Joel A. Jaffe 2025-06-16

// Single macro to switch between desktop and Allosphere configurations
#define DESKTOP

#ifdef DESKTOP
  // Desktop configuration
  #define SAMPLE_RATE 48000
  #define AUDIO_CONFIG SAMPLE_RATE, 128, 2, 8
  #define SPATIALIZER_TYPE al::AmbisonicsSpatializer
  #define SPEAKER_LAYOUT al::StereoSpeakerLayout()
#else
  // Allosphere configuration
  #define SAMPLE_RATE 44100
  #define AUDIO_CONFIG SAMPLE_RATE, 256, 60, 9
  #define SPATIALIZER_TYPE al::Dbap
  #define SPEAKER_LAYOUT al::AlloSphereSpeakerLayoutCompensated()
#endif

#include "al/app/al_DistributedApp.hpp"
#include "al/scene/al_DistributedScene.hpp"
#include "al/math/al_Random.hpp"
#include "al/sound/al_Ambisonics.hpp"
#include "al/sound/al_Dbap.hpp"
#include "al/sphere/al_AlloSphereSpeakerLayout.hpp"

#include "../gimmel/include/gimmel.hpp"

class SimpleVoice : public al::PositionedVoice {
private:
  giml::SinOsc<float> mOsc{SAMPLE_RATE};
  al::Mesh mMesh;
  bool timeToDie = false;

public:

  void init() override {
    mOsc.setFrequency(55.f * al::rnd::uniformi(1, 5)); // random octave of A
    al::addSphere(mMesh);
  }

  void onProcess(al::AudioIOData& io) override {
    for (auto sample = 0; sample < io.framesPerBuffer(); sample++) {
      io.out(0, sample) = mOsc.processSample();
    }

    if (timeToDie) {
      this->free();
    }
  }

  void update(double dt = 0) override {}
  void onProcess(al::Graphics& g) override {
    g.draw(mMesh);
  }

  void onTriggerOn() override {
    timeToDie = false;
  }

  void onTriggerOff() override {
    timeToDie = true;
  }
};

class MyApp : public al::DistributedApp {
private:
  al::DistributedScene mDistributedScene;
  al::Pose mListenerPose{0.f};

public:
  void onInit() override {

    // prepare scene
    mDistributedScene.verbose(true);
    mDistributedScene.registerSynthClass<SimpleVoice>();
    this->registerDynamicScene(mDistributedScene);
    auto speakers = SPEAKER_LAYOUT; 
    mDistributedScene.setSpatializer<SPATIALIZER_TYPE>(speakers);
    mDistributedScene.distanceAttenuation().law(al::ATTEN_NONE);
    mDistributedScene.prepare(audioIO());

    // Set camera position and orientation
    if (isPrimary()) {
      nav().pos(al::Vec3d(35, 0.000000, 49));
      nav().quat(al::Quatd(1.0, 0.000000, 0.325568, 0.000000));
    }
  }

  void onCreate() override {}
  void onAnimate(double dt) override {} 
  
  void onDraw(al::Graphics& g) override {
    g.clear(0);
    mDistributedScene.render(g);
  }

  void onSound(al::AudioIOData& io) override {
    io.zeroOut(); // clear outputs... should be done?
    mDistributedScene.listenerPose(mListenerPose); // seg faults
    mDistributedScene.render(io);
  }

  void onMessage(al::osc::Message& m) override {}

  bool onKeyDown(const al::Keyboard& k) override {
    if (isPrimary() && k.key() == ' ') { // Start a new voice on space bar
      auto* freeVoice = mDistributedScene.getVoice<SimpleVoice>();
      al::Pose pose;
      pose.vec().x = al::rnd::uniform(2);
      pose.vec().y = al::rnd::uniform(2);
      pose.vec().z = -10.0 + al::rnd::uniform(6);
      freeVoice->setPose(pose);
      mDistributedScene.triggerOn(freeVoice);
    }
    return true;
  }
};

int main() {
  MyApp app;
  app.title("Augraff");
  app.configureAudio(AUDIO_CONFIG);
  app.start();
  return 0;
}