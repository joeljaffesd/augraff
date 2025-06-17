// Joel A. Jaffe 2025-06-16

// Single macro to switch between desktop and Allosphere configurations
#define DESKTOP

#define CONSOLE_OUT(x) std::cout << x << std::endl;

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
#include "al/app/al_GUIDomain.hpp"

#include "../gimmel/include/gimmel.hpp"

class SimpleVoice : public al::PositionedVoice {
private:
  giml::SinOsc<float> mOsc{SAMPLE_RATE};
  al::Mesh mMesh;
  al::ParameterVec3 mVelocity{"velocity", ""};
  bool timeToDie = false;

public:

  void init() override {
    this->registerParameters(mPose, mVelocity);
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

  void update(double dt = 0) override {
    // integrate velocity 
    mPose = al::Pose(pose().pos() + mVelocity.get());
    mVelocity = mVelocity.get() * 0.6f; // friction 
  }

  void onProcess(al::Graphics& g) override {
    g.draw(mMesh);
  }

  void onTriggerOn() override {
    timeToDie = false;
  }

  void onTriggerOff() override {
    timeToDie = true;
  }

  void velocity(al::Vec3f vel) {
    mVelocity = vel;
  }

  al::Vec3f velocity() {
    return mVelocity.get();
  }

};

void flushVoices(al::DistributedScene& scene, float springConstant, float simScale) {
  auto* voice = scene.getActiveVoices();
  while (voice) {
    auto* nextVoice = voice->next;
    if (auto posVoice = dynamic_cast<SimpleVoice*>(voice)) { // cast to SimpleVoice

      // get position 
      al::Vec3f pos = posVoice->pose().pos();

      float dis = 0; // initialize dis
      al::Vec3f direction = 0; // initialize direction
      al::Vec3f acceleration = 0; // initialize acceleration

      direction *= 0; // set direction to 0
      direction += al::Vec3f(0, -simScale, 0); // direction = j particle 
      direction -= pos; // direction -= current particle
      dis = direction.mag(); // Euclidian distance between particles
      direction.normalize(); // normalize to unit vector

      // free if at bottom
      if (dis < 0.1f) {
        voice->free();
      }

      // apply force
      acceleration = direction; 
      acceleration *= springConstant; // scale by K

      // update each velocity 
      posVoice->velocity(posVoice->velocity() + acceleration);
    }
    voice = nextVoice;
  }
}

void wrapToSphere(al::DistributedScene& scene, float springConstant, float simScale) {
  auto* voice = scene.getActiveVoices();
  unsigned voiceCounter = 0;
  while (voice) {
    voiceCounter++;
    auto* nextVoice = voice->next;
    if (auto posVoice = dynamic_cast<SimpleVoice*>(voice)) { // cast to SimpleVoice

      // get position 
      al::Vec3f pos = posVoice->pose().pos();
      float mag = pos.mag();

      // wrap to sphere with springs
      float springForceMag = springConstant * (mag - simScale); // create sphere 
      al::Vec3f normalizedNegative = -pos / mag; // create sphere
      al::Vec3f acceleration = springForceMag * normalizedNegative; // create sphere
      
      // update each velocity 
      posVoice->velocity(posVoice->velocity() + acceleration);
    }
    voice = nextVoice; // next voice
  }
}

class MyApp : public al::DistributedApp {
private:
  al::DistributedScene mDistributedScene;
  al::Pose mListenerPose{0.f};

  al::Parameter sphereRadius{"sphereRadius", "", 15.f, 0.f, 30.f};
  al::Parameter springConstant{"springConstant", "", 0.111f, 0.f, 1.f};

public:
  void onInit() override {
    if (isPrimary()) {
      auto GUIdomain = al::GUIDomain::enableGUI(defaultWindowDomain());
      auto &gui = GUIdomain->newGUI();
      gui.add(sphereRadius); // add parameter to GUI
      gui.add(springConstant); // add parameter to GUI
    }

    // prepare scene
    mDistributedScene.verbose(true);
    mDistributedScene.registerSynthClass<SimpleVoice>();
    this->registerDynamicScene(mDistributedScene);


    if (isPrimary()) {
      auto speakers = SPEAKER_LAYOUT; 
      mDistributedScene.setSpatializer<SPATIALIZER_TYPE>(speakers);
      mDistributedScene.distanceAttenuation().law(al::ATTEN_NONE);
      mDistributedScene.prepare(audioIO());
    }

    // Set camera position and orientation
    if (isPrimary()) {
      nav().pos(al::Vec3d(35, 0.000000, 49));
      nav().quat(al::Quatd(1.0, 0.000000, 0.325568, 0.000000));
    }
  }

  void onCreate() override {}

  bool shouldFlush = false;
  void onAnimate(double dt) override {
    if (isPrimary()) {
      wrapToSphere(mDistributedScene, springConstant, sphereRadius);

      if (shouldFlush) {
        if (!mDistributedScene.getActiveVoices()) {
          shouldFlush = false;
        }
        flushVoices(mDistributedScene, springConstant, sphereRadius);
      }

      mDistributedScene.update(dt);
    }
  } 
  
  void onDraw(al::Graphics& g) override {
    g.lens().eyeSep(0); // disable stereo
    g.clear(0);
    mDistributedScene.render(g);
  }

  void onSound(al::AudioIOData& io) override {
    if (isPrimary()) {
      io.zeroOut(); // clear outputs... should be done?
      mDistributedScene.listenerPose(mListenerPose); // seg faults
      mDistributedScene.render(io);      
    }
  }

  void onMessage(al::osc::Message& m) override {}

  bool onKeyDown(const al::Keyboard& k) override {
    if (isPrimary()) { 
      if (k.key() == ' ') { // Start a new voice on space bar
        auto* freeVoice = mDistributedScene.getVoice<SimpleVoice>();
        al::Pose pose;
        pose.vec().x = al::rnd::uniform(2);
        pose.vec().y = al::rnd::uniform(2);
        pose.vec().z = -10.0 + al::rnd::uniform(6);
        freeVoice->setPose(pose);
        mDistributedScene.triggerOn(freeVoice);
      }
      else if (k.key() == 'f') { // flush voices
        shouldFlush = true;
      }
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