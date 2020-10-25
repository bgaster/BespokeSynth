//
//  AAModule.h
//  modularSynth
//
//  Created by Benedict R. Gaster on 2020-10-15.
//
//

#ifndef __modularSynth__AATest__
#define __modularSynth__AATest__

#include <functional>
#include <iostream>
#include <vector>
#include <tuple>

#include "IAudioProcessor.h"
#include "PolyphonyMgr.h"
#include "SingleOscillatorVoice.h"
#include "ADSR.h"
#include "INoteReceiver.h"
#include "IDrawableModule.h"
#include "Slider.h"
#include "DropdownList.h"
#include "ADSRDisplay.h"
#include "Checkbox.h"
#include "RadioButton.h"
#include "Oscillator.h"
#include "ofxJSONElement.h"

#include "aa_wasmtime_c.h"

class ofxJSONElement;

namespace AAModuleLookup {
    void GetAvailableAAModules(vector<string>& modules, bool rescan);
}

class AAADSR 
{
public:
    AAADSR(
        AAModule* module,
        int node, 
        int aIndex, float aInit, float aMin, float aMax,
        int dIndex, float dInit, float dMin, float dMax,
        int sIndex, float sInit, float sMin, float sMax,
        int rIndex, float rInit, float rMin, float rMax,
        IDrawableModule *owner, std::string name, int x, int y, int w, int h) :
            mAAModule{module}
          , mADSRDisplay{nullptr}
          , mADSR{
              map_to(aInit, aMin, aMax, 1.f, 1000.f),
              map_to(dInit, dMin, dMax, 0.f, 1000.f),
              map_to(sInit, sMin, sMax, 0.f, 1.f),
              map_to(rInit, rMin, rMax, 1.f, 1000.f)}
          , mNode{node}
          , mAIndex{aIndex}
          , mAMin{aMin}
          , mAMax{aMax}
          , mDIndex{dIndex}
          , mDMin{dMin}
          , mDMax{dMax}
          , mSIndex{sIndex}
          , mSMin{sMin}
          , mSMax{sMax}
          , mRIndex{rIndex}
          , mRMin{rMin}
          , mRMax{rMax}
    {
        auto adsrUpdate = [&](UpdateADSRParam p) {
            switch (p) {
                case kUpdateAttack: {
                    updateAttack();
                }
                case kUpdateDecaySustain: {
                    updateDecay();
                    updateSustain();
                }
                case kUpdateRelease: {
                    updateRelease();
                }
                default: { }
            }
        };

        mADSRDisplay = new ADSRDisplay(owner,name.c_str(), x, y, w, h, &mADSR, adsrUpdate);
        assert(mADSRDisplay != nullptr);
    }

    void draw() const {
        mADSRDisplay->Draw();
    }

    void setActive(bool active) const {
        mADSRDisplay->SetActive(active);
    }

    void floatSliderUpdated(AAModule* module, FloatSlider* slider) {
        if (mADSRDisplay->isASlider(slider)) {
            updateAttack();
        }
        if (mADSRDisplay->isDSlider(slider)) {
            updateDecay();
        }
        if (mADSRDisplay->isSSlider(slider)) {
            updateSustain();
        }
        if (mADSRDisplay->isRSlider(slider)) {
            updateRelease();
        }
    }
private:
    AAModule *mAAModule;
    ADSRDisplay *mADSRDisplay;
    ::ADSR mADSR;
    int mNode; 
    int mAIndex;
    float mAMin; 
    float mAMax;
    int mDIndex;
    float mDMin; 
    float mDMax;
    int mSIndex; 
    float mSMin; 
    float mSMax;
    int mRIndex; 
    float mRMin;
    float mRMax;

    float map_to(float x, float in_min, float in_max, float out_min, float out_max)
    {
        return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    }

    void updateAttack() {
        auto attack = map_to(mADSR.GetA(), 1.f, 1000.f, mAMin, mAMax);
        set_param_float(mAAModule, mNode, mAIndex, attack);
    }
    
    void updateDecay() {
        auto decay = map_to(mADSR.GetD(), 0.f, 1000.f, mDMin, mDMax);
        set_param_float(mAAModule, mNode, mDIndex, decay);
    }
    
    void updateSustain() {
        auto sustain = map_to(mADSR.GetS(), 0.f, 1.f, mSMin, mSMax);
        set_param_float(mAAModule, mNode, mSIndex, sustain);
    }
    
    void updateRelease() {
        auto release = map_to(mADSR.GetS(), 0.f, 1.f, mRMin, mRMax);
        set_param_float(mAAModule, mNode, mRIndex, release);
    }
};

class AADropdownList 
{
public:
    AADropdownList(
        std::vector<std::pair<std::string, std::vector<std::tuple<int, int, float, std::function<void ()>>>>> params, 
        IDropdownListener* owner, const char* name, int x, int y, float width = -1) :
          mDropdownList{nullptr}
        , mVar{0}
    {
        mDropdownList = new DropdownList(owner,name, x, y, &mVar);
        for (int i = 0; i < params.size(); i++) {
            mDropdownList->AddLabel(params[i].first.c_str(), i);
            mParams.push_back(params[i].second);
        }
    }
    AADropdownList(
        std::vector<std::pair<std::string, std::vector<std::tuple<int, int, float, std::function<void ()>>>>> params, 
        IDropdownListener* owner, const char* name, IUIControl* anchor, 
        AnchorDirection anchorDirection, float width = -1) :
          mDropdownList{nullptr}
        , mVar{0}
    {
        mDropdownList = new DropdownList(owner, name, anchor, anchorDirection, &mVar);
        for (int i = 0; i < params.size(); i++) {
            mDropdownList->AddLabel(params[i].first.c_str(), i);
            mParams.push_back(params[i].second);
        }
    }

    void draw() const 
    {
        mDropdownList->Draw();
    }

    void updated(AAModule* module, DropdownList* list) 
    {
        if (mDropdownList == list) {
            for (auto& p: mParams[mVar]) {
                set_param_float(module, std::get<0>(p), std::get<1>(p), std::get<2>(p));
                std::get<3>(p)();
            }
        }
    }

    DropdownList* getDropdownList() const
    {
        return mDropdownList;
    }
private:
    DropdownList* mDropdownList;
    std::vector<std::vector<std::tuple<int, int, float, std::function<void ()>>>> mParams; 
    int mVar;
};

class AASlider 
{
public:
    AASlider(int node, int index, IFloatSliderListener* owner,
             std::string name, int x, int y, int w, int h, float init, float min, float max, int digits = -1) :
          mSlider{nullptr}
        , mValue{init}
        , mNode{node}
        , mIndex{index}
        , mName{name}
    {
        mSlider = new FloatSlider(
            owner, 
            name.c_str(),
            x, y, w, h, 
            &mValue, 
            min, 
            max,
            digits);
    }

    AASlider(int node, int index, IFloatSliderListener *owner,
            std::string name, 
            FloatSlider* anchorSlider, 
            AnchorDirection anchorDirection, int w, int h, float init, float min, float max, int digits = -1) :
          mSlider{nullptr}
        , mValue{init}
        , mNode{node}
        , mIndex{index}
        , mName{name}
    {
        mSlider = new FloatSlider(
                owner, 
                name.c_str(),
                anchorSlider, 
                anchorDirection,
                w, h, 
                &mValue, 
                min, max,
                digits);
    }

    void draw() const {
        mSlider->Draw();
    }

    void setParamFloat(AAModule* module) const {
        set_param_float(module, mNode, mIndex, mValue);
    }

    bool isSlider(FloatSlider* slider) const {
        return mSlider==slider;
    }

    FloatSlider* getSlider() const {
        return mSlider;
    }

    std::string getName() const {
        return mName;
    }

    void setValue(float v) {
        mValue = v;
    }
private:
    FloatSlider* mSlider;
    float mValue;
    int mNode;
    int mIndex;
    std::string mName;
};

class AAISlider 
{
public:
    AAISlider(int node, int index, IIntSliderListener* owner,
             std::string name, int x, int y, int w, int h, 
             int init, int min, int max,
             std::function<float (int)> map2Float) :
          mSlider{nullptr}
        , mValue{init}
        , mNode{node}
        , mIndex{index}
        , mMap2Float{map2Float}
        , mName{name}
    {
        mSlider = new IntSlider(
            owner, 
            name.c_str(),
            x, y, w, h, 
            &mValue, 
            min, 
            max);
    }

    AAISlider(int node, int index, IIntSliderListener *owner,
            std::string name, 
            IntSlider* anchorSlider, 
            AnchorDirection anchorDirection, 
            int w, int h, int init, int min, int max,
            std::function<float (int)> map2Float) :
          mSlider{nullptr}
        , mValue{init}
        , mNode{node}
        , mIndex{index}
        , mMap2Float{map2Float}
        , mName{name}
    {
        mSlider = new IntSlider(
                owner, 
                name.c_str(),
                anchorSlider, 
                anchorDirection,
                w, h, 
                &mValue, 
                min, max);
    }

    void draw() const {
        mSlider->Draw();
    }

    void setParamInt(AAModule* module) const {
        set_param_float(module, mNode, mIndex, mMap2Float(mValue));
    }

    bool isSlider(IntSlider* slider) const {
        return mSlider==slider;
    }

    IntSlider* getSlider() const {
        return mSlider;
    }

    std::string getName() const {
        return mName;
    }

    void setValue(int v) {
        mValue = v;
    }
private:
    IntSlider* mSlider;
    int mValue;
    int mNode;
    int mIndex;
    std::function<float (int)> mMap2Float;
    std::string mName;
};

class AATest;

class AATest : public IAudioProcessor, public INoteReceiver, public IDrawableModule, 
               public IDropdownListener, public IFloatSliderListener, public IIntSliderListener, 
               public IRadioButtonListener
{
public:
   AATest();
   ~AATest();
   static IDrawableModule* Create() { return new AATest(); }
   
   string GetTitleLabel() override { return mLabel; }
   void CreateUIControls() override;
   
   //IAudioSource
   void Process(double time) override;
   void SetEnabled(bool enabled) override;
   
   //INoteReceiver
   void PlayNote(double time, int pitch, int velocity, int voiceIdx = -1, ModulationParameters modulation = ModulationParameters()) override;
   void SendCC(int control, int value, int voiceIdx = -1) override {}
   
   void DropdownUpdated(DropdownList* list, int oldVal) override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal) override;
   void IntSliderUpdated(IntSlider* slider, int oldVal) override;
   void CheckboxUpdated(Checkbox* checkbox) override;
   void RadioButtonUpdated(RadioButton* list, int oldVal) override;
   
   bool HasDebugDraw() const override { return true; }
   
   virtual void LoadLayout(const ofxJSONElement& moduleInfo) override;
   virtual void SetUpFromSaveData() override;

   void SetModule(string moduleName);
   void LoadModule(string moduleName);
private:

   //IDrawableModule
   void DrawModule() override;
   void DrawModuleUnclipped() override;
   void GetModuleDimensions(float& width, float& height) override;
   bool Enabled() const override { return mEnabled; }

   void UpdateADSRDisplays();

   void CreateModuleControls();
   void DrawLogo(float  x,  float y, float scaleX, float scaleY);

   std::string mLabel;
   float mWidth;
   float mHeight;
   ofxJSONElement mJSONUi;

   NoteInputBuffer mNoteInputBuffer;
   
    // sequences of UI elements for AA module
    std::vector<AASlider*> mFSliders;
    std::vector<AAISlider*> mISliders;
    std::vector<AAADSR*> mAAADSRs;
    std::vector<AADropdownList*> mAADropdownLists;

    // tempory buffer for audio output date from AA world
    ChannelBuffer mWriteBuffer;

    // function to compute audio in AA world
    std::function<void (int, ChannelBuffer*)> mModuleCompute;

    int mNumInputs;
    int mNumOutputs;

   string mDebugLines;
    
    // instance of loaded AA module
    AAModule * aaModule;

    AnchorDirection toAnchorDirection(std::string s) const {
        if (s.compare("anchored_below") == 0) {
            return kAnchor_Below;
        }
        else if (s.compare("anchored_right") == 0) {
            return kAnchor_Right;
        }
        else {
            return kAnchor_Right_Padded;
        }
    }
};

#endif /* defined(__modularSynth__AATest__) */

