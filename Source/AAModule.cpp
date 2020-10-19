//
//  AAText.cpp
//  modularSynth
//
//  Created by Benedict Gaster on 04/10/2020.
//
//

#include <utility>
	
#include "AAModule.h"
#include "OpenFrameworksPort.h"
#include "SynthGlobals.h"
#include "IAudioReceiver.h"
#include "ofxJSONElement.h"
#include "ModularSynth.h"
#include "Profiler.h"


namespace {
   float map_to(float x, float in_min, float in_max, float out_min, float out_max)
    {
        return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    }
}

namespace AAModuleLookup {
   namespace {
      vector<std::pair<string,string>> sAAModuleList;
   }

   void GetAvailableAAModules(vector<string>& modules, bool rescan) {
      if (rescan) {
         auto modulesJSON = aa_get_modules("http://127.0.0.1:8000");

         auto mJSON = ofxJSONElement{std::string{modulesJSON}};

         if (mJSON.isObject()) {
            assert(mJSON["modules"].isArray());
            if (mJSON["modules"].isArray()) {
               auto modules = mJSON["modules"];

               for (auto b = modules.begin(); b != modules.end(); b++) {
                  assert((*b)["name"].isString());
                  auto name = (*b)["name"].asString();

                  assert((*b)["json_url"].isString());
                  auto json_url = (*b)["json_url"].asString();

                  sAAModuleList.push_back(pair<string,string>(name, json_url));
               }
            }
         }
      }
         
      for (auto m: sAAModuleList) {
            modules.push_back(m.first);
      }
   }
}

AATest::AATest()
: IAudioProcessor(gBufferSize)
, mLabel{"empty"}
, mWidth{0.f}
, mHeight{0.f}
, mNoteInputBuffer(this)
, mWriteBuffer(gBufferSize)
, aaModule(nullptr)
{ 
   
}

void AATest::CreateModuleControls() {
   if (mJSONUi["widgets"].isArray()) {
      auto widgets = mJSONUi["widgets"];

      for (auto b = widgets.begin(); b != widgets.end(); b++) {
         assert((*b)["type"].isString());
         auto widgetType = (*b)["type"].asString();
         
         if (widgetType.compare("float_slider") == 0) {
            assert((*b)["name"].isString());
            auto name = (*b)["name"].asString();

            assert((*b)["w"].isInt() && (*b)["h"].isInt());
            int w = (*b)["w"].asInt();
            int h = (*b)["h"].asInt();

            assert((*b)["init"].isDouble() && (*b)["min"].isDouble() && (*b)["max"].isDouble());
            float init = static_cast<float>((*b)["init"].asDouble());
            float min = static_cast<float>((*b)["min"].asDouble());
            float max = static_cast<float>((*b)["max"].asDouble());

            assert((*b)["node"].isInt() && (*b)["index"].isInt());
            int node = (*b)["node"].asInt();
            int index = (*b)["index"].asInt();

            int digits = -1;
            if ((*b)["digits"].isInt()) {
               digits = (*b)["digits"].asInt();
            }
            
            // is it anchored slider?
            if ((*b)["anchored"].isString()) {
               auto anchor = toAnchorDirection((*b)["anchored"].asString());
               if (mFSliders.size() > 0) {
                  auto slider = new AASlider(
                     node, index, this, name, mFSliders[mFSliders.size()-1]->getSlider(), 
                     anchor, w, h, init, min, max, digits);
                  mFSliders.push_back(slider);
               }
            }
            else {
               assert((*b)["x"].isInt() && (*b)["y"].isInt());
               int x = (*b)["x"].asInt();
               int y = (*b)["y"].asInt();
               auto slider = new AASlider(node, index, this, name, x, y, w, h, init, min, max, digits);
               mFSliders.push_back(slider);
            }
         }
         else if (widgetType.compare("int_slider") == 0) {
            assert((*b)["name"].isString());
            auto name = (*b)["name"].asString();

            assert((*b)["w"].isInt() && (*b)["h"].isInt());
            int w = (*b)["w"].asInt();
            int h = (*b)["h"].asInt();

            assert((*b)["init"].isInt() && (*b)["min"].isInt() && (*b)["max"].isInt());
            float init = (*b)["init"].asInt();
            float min = (*b)["min"].asInt();
            float max = (*b)["max"].asInt();

            assert((*b)["node"].isInt() && (*b)["index"].isInt());
            int node = (*b)["node"].asInt();
            int index = (*b)["index"].asInt();

            auto map2Float = std::function<float (int)>(
               [](int v) -> float { return static_cast<float>(v); });

            if ((*b)["map_to_float"].isObject()) {
               auto m = (*b)["map_to_float"];
               if (m != Json::nullValue) {
                  assert(m["min"].isDouble() && m["max"].isDouble());
                  float minF = static_cast<float>(m["min"].asDouble());
                  float maxF = static_cast<float>(m["max"].asDouble());

                  map2Float = [=](int v) -> float {
                     return map_to(static_cast<float>(v), static_cast<float>(min), static_cast<float>(max), minF, maxF);
                  };
               }
            }
            
            // is it anchored slider?
            if ((*b)["anchored"].isString()) {
               auto anchor = toAnchorDirection((*b)["anchored"].asString());
               if (mISliders.size() > 0) {
                  auto slider = new AAISlider(
                     node, index, this, name, mISliders[mISliders.size()-1]->getSlider(), 
                     anchor, w, h, init, min, max, map2Float);
                  mISliders.push_back(slider);
               }
            }
            else {
               assert((*b)["x"].isInt() && (*b)["y"].isInt());
               int x = (*b)["x"].asInt();
               int y = (*b)["y"].asInt();
               auto slider = new AAISlider(node, index, this, name, x, y, w, h, init, min, max, map2Float);
               mISliders.push_back(slider);
            }
         }
         else if (widgetType.compare("adsr") == 0) {
            assert((*b)["name"].isString());
            auto name = (*b)["name"].asString();

            assert((*b)["x"].isInt() && (*b)["y"].isInt());
            int x = (*b)["x"].asInt();
            int y = (*b)["y"].asInt();

            assert((*b)["w"].isInt() && (*b)["h"].isInt());
            int w = (*b)["w"].asInt();
            int h = (*b)["h"].asInt();

            assert((*b)["indexA"].isInt() && (*b)["initA"].isDouble() && (*b)["minA"].isDouble() && (*b)["maxA"].isDouble());
            int indexA = (*b)["indexA"].asInt();
            float initA = static_cast<float>((*b)["initA"].asDouble());
            float minA = static_cast<float>((*b)["minA"].asDouble());
            float maxA = static_cast<float>((*b)["maxA"].asDouble());

            assert((*b)["indexD"].isInt() && (*b)["initD"].isDouble() && (*b)["minD"].isDouble() && (*b)["maxD"].isDouble());
            int indexD = (*b)["indexD"].asInt();
            float initD = static_cast<float>((*b)["initD"].asDouble());
            float minD = static_cast<float>((*b)["minD"].asDouble());
            float maxD = static_cast<float>((*b)["maxD"].asDouble());

            assert((*b)["indexS"].isInt() && (*b)["initS"].isDouble() && (*b)["minS"].isDouble() && (*b)["maxS"].isDouble());
            int indexS = (*b)["indexS"].asInt();
            float initS = static_cast<float>((*b)["initS"].asDouble());
            float minS = static_cast<float>((*b)["minS"].asDouble());
            float maxS = static_cast<float>((*b)["maxS"].asDouble());

            assert((*b)["indexR"].isInt() && (*b)["initR"].isDouble() && (*b)["minR"].isDouble() && (*b)["maxR"].isDouble());
            int indexR = (*b)["indexR"].asInt();
            float initR = static_cast<float>((*b)["initR"].asDouble());
            float minR = static_cast<float>((*b)["minR"].asDouble());
            float maxR = static_cast<float>((*b)["maxR"].asDouble());

            assert((*b)["node"].isInt());
            int node = (*b)["node"].asInt();

            auto mAAADSR = new AAADSR(
               aaModule, node,
               indexA, initA, minA, maxA,
               indexD, initD, minD, maxD,
               indexS, initS, minS, maxS,
               indexR, initR, minR, maxR,
               this, name.c_str(), x, y, w, h);

            mAAADSRs.push_back(mAAADSR);
         }
         else if (widgetType.compare("dropdown") == 0) {
            assert((*b)["name"].isString());
            auto name = (*b)["name"].asString();

            std::vector<std::pair<std::string, std::vector<std::tuple<int, int, float>>>> paramsR;

            if ((*b)["params"].isArray()) {
               auto params = (*b)["params"];

               for (auto p = params.begin(); p != params.end(); p++) {
                  assert((*p)["name"].isString());
                  auto name = (*p)["name"].asString();

                  std::vector<std::tuple<int, int, float>> valuesR;
                  if ((*p)["values"].isArray()) {
                     auto values = (*p)["values"];
                     for (auto v = values.begin(); v != values.end(); v++) {
                        assert((*v)["node"].isInt() && (*v)["index"].isInt() && (*v)["value"].isDouble());
                        int node = (*v)["node"].asInt();
                        int index = (*v)["index"].asInt();
                        float value = static_cast<float>((*v)["value"].asDouble());
                        valuesR.push_back(std::make_tuple(node, index, value));
                     }
                  }

                  paramsR.push_back(std::pair<std::string, std::vector<std::tuple<int, int, float>>>{name, valuesR});
               }
            }

            if ((*b)["anchored"].isString()) {
               auto anchor = toAnchorDirection((*b)["anchored"].asString());
               if (mAADropdownLists.size() > 0) {
                  auto dropdown = new AADropdownList(
                     paramsR, this, name.c_str(), mAADropdownLists[mAADropdownLists.size()-1]->getDropdownList(), anchor);
                  mAADropdownLists.push_back(dropdown);
               }
            }
            else {
               assert((*b)["x"].isInt() && (*b)["y"].isInt());
               int x = (*b)["x"].asInt();
               int y = (*b)["y"].asInt();
               auto dropdown = new AADropdownList(paramsR, this, name.c_str(), x, y);
               mAADropdownLists.push_back(dropdown);
            }
         }
      }
   }

   UpdateADSRDisplays();
}

void AATest::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   if (aaModule != nullptr) {
      CreateModuleControls();
   }
}

AATest::~AATest()
{
    if (aaModule != nullptr) {
        aa_module_delete(aaModule);
    }
}

void AATest::Process(double time)
{
   PROFILER(AATest);

   if (!mEnabled || GetTarget() == nullptr || !aaModule)
      return;
   
   GetBuffer()->SetNumActiveChannels(mNumInputs);

   mNoteInputBuffer.Process(time);
   
   ComputeSliders(0);

   int bufferSize = GetTarget()->GetBuffer()->BufferSize();
   assert(bufferSize == gBufferSize);
   
   mWriteBuffer.Clear();
   ChannelBuffer* out = &mWriteBuffer;

   // Now handle computing audio in AA world
   // current limitation is support for only max 2 input and max 2 outputs
   if (mNumInputs > 0) {
      if (mNumInputs == 2) {

      }
      else {
         if (mNumOutputs == 2) {
            aa_module_compute_one_two_non(
               aaModule, 
               bufferSize, 
               GetBuffer()->GetChannel(0), 
               out->GetChannel(0), out->GetChannel(1));
         }
      }
   }
   else if (mNumOutputs == 2) {
      aa_module_compute_zero_two_non(aaModule, bufferSize, out->GetChannel(0), out->GetChannel(1));
   } 
   else {
      aa_module_compute_zero_one(aaModule, bufferSize, out->GetChannel(0));
   }

   GetBuffer()->Clear();
   
   SyncOutputBuffer(mWriteBuffer.NumActiveChannels());
   for (int ch=0; ch<mWriteBuffer.NumActiveChannels(); ++ch)
   {
      GetVizBuffer()->WriteChunk(mWriteBuffer.GetChannel(ch),mWriteBuffer.BufferSize(), ch);
      Add(GetTarget()->GetBuffer()->GetChannel(ch), mWriteBuffer.GetChannel(ch), gBufferSize);
   }
}

void AATest::PlayNote(double time, int pitch, int velocity, int voiceIdx, ModulationParameters modulation)
{
   if (aaModule == nullptr)
      return;

   if (!NoteInputBuffer::IsTimeWithinFrame(time))
   {
      mNoteInputBuffer.QueueNote(time, pitch, velocity, voiceIdx, modulation);
      return;
   }
   
   if (velocity > 0)
   {
      aa_module_handle_note_on(aaModule, pitch, velocity/127.0f);
   }
   else
   {
      aa_module_handle_note_off(aaModule, pitch, 0.0);
   }
   
   if (mDrawDebug)
   {
      vector<string> lines = ofSplitString(mDebugLines, "\n");
      mDebugLines = "";
      const int kNumDisplayLines = 10;
      for (int i=0; i<kNumDisplayLines-1; ++i)
      {
         int lineIndex = (int)lines.size()-(kNumDisplayLines-1) + i;
         if (lineIndex >= 0)
            mDebugLines += lines[lineIndex] + "\n";
      }
      mDebugLines += "PlayNote("+ofToString(time/1000)+", "+ofToString(pitch)+", "+ofToString(velocity)+", "+ofToString(voiceIdx)+")";
   }
}

void AATest::SetEnabled(bool enabled)
{
   mEnabled = enabled;
}

void AATest::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;
   
   // draw float sliders
   for (auto slider: mFSliders) {
      slider->draw();
   }

   // draw int sliders
   for (auto slider: mISliders) {
      slider->draw();
   }

   // draw drop downs
   for (auto dropdown: mAADropdownLists) {
      dropdown->draw();
   }
   
   // draw ADSRs
   for (auto adsr: mAAADSRs) {
      adsr->draw();
   }
}
 
void AATest::DrawModuleUnclipped()
{
   if (mDrawDebug)
   {
      //mPolyMgr.DrawDebug(200, 0);
      DrawTextNormal(mDebugLines, 0, 160);
   }
}

void AATest::GetModuleDimensions(float& width, float& height)
{
    width = mWidth;
    height = mHeight;
}

void AATest::UpdateADSRDisplays()
{
   for (auto adsr: mAAADSRs) {
      adsr->setActive(true);
   }
}

void AATest::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo);
   //mModuleSaveData.LoadFloat("vol", moduleInfo, .5, mVolSlider);
   //mModuleSaveData.LoadEnum<OscillatorType>("osc", moduleInfo, kOsc_Sin, mOscSelector);
   //mModuleSaveData.LoadFloat("detune", moduleInfo, 1, mDetuneSlider);
   mModuleSaveData.LoadBool("pressure_envelope", moduleInfo);
   mModuleSaveData.LoadInt("voicelimit", moduleInfo, -1, -1, kNumVoices);

   SetUpFromSaveData();
}

void AATest::SetUpFromSaveData()
{
   SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("target")));
   int voiceLimit = mModuleSaveData.GetInt("voicelimit");
   // if (voiceLimit > 0)
   //    mPolyMgr.SetVoiceLimit(voiceLimit);
}


void AATest::DropdownUpdated(DropdownList* list, int oldVal)
{
   for (auto dropdown: mAADropdownLists) {
      dropdown->updated(aaModule, list);
   }
}

void AATest::RadioButtonUpdated(RadioButton* list, int oldVal)
{
   // if (list == mADSRModeSelector)
   // {
   //    UpdateADSRDisplays();
   // }
}

void AATest::FloatSliderUpdated(FloatSlider* slider, float oldVal)
{
   // check ADSR sliders
   for (auto adsr: mAAADSRs) {
      adsr->floatSliderUpdated(aaModule, slider);
   }
   
   for (auto s: mFSliders) {
      if (s->isSlider(slider)) {
         s->setParamFloat(aaModule);
      }
   }
}

void AATest::IntSliderUpdated(IntSlider* slider, int oldVal)
{
   for (auto s: mISliders) {
      if (s->isSlider(slider)) {
         s->setParamInt(aaModule);
      }
   }
}

void AATest::CheckboxUpdated(Checkbox* checkbox)
{
}

void AATest::SetModule(string moduleName) {
   ofLog() << "loading AA Module: " << moduleName;

   for (auto m: AAModuleLookup::sAAModuleList) {
      if (m.first.compare(moduleName) == 0) {
         // init AA module
         aaModule = aa_module_new("http://127.0.0.1:8000", m.second.c_str());

         if (aaModule == nullptr) {
            ofLog() << "failed to load AA Module: " << moduleName;
            return;
         }

         aa_module_init(aaModule, gSampleRate);
         
         mNumInputs = aa_module_get_number_inputs(aaModule);
         mNumOutputs = aa_module_get_number_outputs(aaModule);
         mWriteBuffer.SetNumActiveChannels(mNumOutputs);
         ofLog() << "aa inputs: " << mNumInputs << "  aa outputs: " << mNumOutputs;
         
         auto jsonUI = get_gui_description(aaModule);

         if (jsonUI == nullptr) {
            ofLog() << "no GUI description for AA Module: " << moduleName;
            return;
         }

         mJSONUi = ofxJSONElement{std::string{jsonUI}};

         if (mJSONUi.isObject()) {
            assert(mJSONUi["name"].isString());
            mLabel = mJSONUi["name"].asString();

            assert(mJSONUi["width"].isDouble() || mJSONUi["width"].isInt());
            assert(mJSONUi["height"].isDouble() || mJSONUi["height"].isInt());
            
            mWidth = static_cast<float>(mJSONUi["width"].asDouble());
            mHeight = static_cast<float>(mJSONUi["height"].asDouble());
         }

         CreateModuleControls();
         return;
      }
   }
}
