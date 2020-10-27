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

#include "nanovg/nanovg.h"
#define NANOVG_GLES2_IMPLEMENTATION
#include "nanovg/nanovg_gl.h"

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

   void GetAvailableAAModules(vector<string>& modules, bool rescan) 
   {
      if (rescan) 
      {
         sAAModuleList.clear();

         //TODO: push URL definition to config file
         auto url = std::string("http://127.0.0.1:8000");
         auto modulesJSON = aa_get_modules(url.c_str());

         if (modulesJSON == nullptr) 
         {
            ofLog() << "Failed to connect to AA server: " << url;
            return;
         }

         auto mJSON = ofxJSONElement{std::string{modulesJSON}};

         if (mJSON.isObject()) 
         {
            assert(mJSON["modules"].isArray());
            if (mJSON["modules"].isArray()) 
            {
               auto modules = mJSON["modules"];

               for (auto b = modules.begin(); b != modules.end(); b++) 
               {
                  assert((*b)["name"].isString());
                  auto name = (*b)["name"].asString();

                  assert((*b)["json_url"].isString());
                  auto json_url = (*b)["json_url"].asString();

                  sAAModuleList.push_back(pair<string,string>(name, json_url));
               }
            }
         }

         for (auto m: sAAModuleList)
         {
            ofxJSONElement root;
            root.open(ofToDataPath("internal/seen_aamodules.json"));
            
            root[m.first] = m.second;

            root.save(ofToDataPath("internal/seen_aamodules.json"), true);
         }
      }
      else  
      {
         auto file = juce::File(ofToDataPath("internal/seen_aamodules.json"));
         if (file.existsAsFile())
         {
            sAAModuleList.clear();
            ofxJSONElement root;
            root.open(ofToDataPath("internal/seen_aamodules.json"));

            ofxJSONElement jsonList = root;

            for (auto it = jsonList.begin(); it != jsonList.end(); ++it)
            {
               string name = it.key().asString();
               string json_url = jsonList[name].asString();
               sAAModuleList.push_back(pair<string,string>(name, json_url));
            }
         }
      }
         
      for (auto m: sAAModuleList) 
      {
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
   if (mJSONUi["widgets"].isObject()) {
      // sliders
      if (mJSONUi["widgets"]["sliders"].isArray()) {
         auto sliders = mJSONUi["widgets"]["sliders"];

         for (auto b = sliders.begin(); b != sliders.end(); b++) {
            assert((*b)["type"].isString());
            auto sliderType = (*b)["type"].asString();
         
            if (sliderType.compare("float_slider") == 0) {
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
                     slider->setParamFloat(aaModule);
                     mFSliders.push_back(slider);
                  }
               }
               else {
                  assert((*b)["x"].isInt() && (*b)["y"].isInt());
                  int x = (*b)["x"].asInt();
                  int y = (*b)["y"].asInt();
                  auto slider = new AASlider(node, index, this, name, x, y, w, h, init, min, max, digits);
                  slider->setParamFloat(aaModule);
                  mFSliders.push_back(slider);
               }
            }
            else if (sliderType.compare("int_slider") == 0) {
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
                     // finally init param in module
                     slider->setParamInt(aaModule);
                  }
               }
               else {
                  assert((*b)["x"].isInt() && (*b)["y"].isInt());
                  int x = (*b)["x"].asInt();
                  int y = (*b)["y"].asInt();
                  auto slider = new AAISlider(node, index, this, name, x, y, w, h, init, min, max, map2Float);
                  mISliders.push_back(slider);
                  // finally init param in module
                  slider->setParamInt(aaModule);
               }
            }
         }
      }
      
      if (mJSONUi["widgets"]["adsrs"].isArray() && mJSONUi["widgets"]["adsrs"] != Json::nullValue) {
         auto adsrs = mJSONUi["widgets"]["adsrs"];

         for (auto b = adsrs.begin(); b != adsrs.end(); b++) {
            assert((*b)["name"].isString());
            auto name = (*b)["name"].asString();

            assert((*b)["x"].isInt() && (*b)["y"].isInt());
            int x = (*b)["x"].asInt();
            int y = (*b)["y"].asInt();

            assert((*b)["w"].isInt() && (*b)["h"].isInt());
            int w = (*b)["w"].asInt();
            int h = (*b)["h"].asInt();

            assert((*b)["indexA"].isInt() && (*b)["initA"].isDouble() && 
                   (*b)["minA"].isDouble() && (*b)["maxA"].isDouble());
            int indexA = (*b)["indexA"].asInt();
            float initA = static_cast<float>((*b)["initA"].asDouble());
            float minA = static_cast<float>((*b)["minA"].asDouble());
            float maxA = static_cast<float>((*b)["maxA"].asDouble());

            assert((*b)["indexD"].isInt() && (*b)["initD"].isDouble() && 
                   (*b)["minD"].isDouble() && (*b)["maxD"].isDouble());
            int indexD = (*b)["indexD"].asInt();
            float initD = static_cast<float>((*b)["initD"].asDouble());
            float minD = static_cast<float>((*b)["minD"].asDouble());
            float maxD = static_cast<float>((*b)["maxD"].asDouble());

            assert((*b)["indexS"].isInt() && (*b)["initS"].isDouble() && 
                   (*b)["minS"].isDouble() && (*b)["maxS"].isDouble());
            int indexS = (*b)["indexS"].asInt();
            float initS = static_cast<float>((*b)["initS"].asDouble());
            float minS = static_cast<float>((*b)["minS"].asDouble());
            float maxS = static_cast<float>((*b)["maxS"].asDouble());

            assert((*b)["indexR"].isInt() && (*b)["initR"].isDouble() && 
                   (*b)["minR"].isDouble() && (*b)["maxR"].isDouble());
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
      }
      
      if (mJSONUi["widgets"]["dropdown"].isArray()) {
         auto dropdowns = mJSONUi["widgets"]["dropdowns"];

         for (auto b = dropdowns.begin(); b != dropdowns.end(); b++) {
            assert((*b)["name"].isString());
            auto name = (*b)["name"].asString();

            std::vector<std::pair<std::string, std::vector<std::tuple<int, int, float, std::function<void ()>>>>> paramsR;

            if ((*b)["params"].isArray()) {
               auto params = (*b)["params"];

               for (auto p = params.begin(); p != params.end(); p++) {
                  assert((*p)["name"].isString());
                  auto name = (*p)["name"].asString();

                  std::vector<std::tuple<int, int, float, std::function<void ()>>> valuesR;
                  if ((*p)["values"].isArray()) {
                     auto values = (*p)["values"];
                     for (auto v = values.begin(); v != values.end(); v++) {
                        assert((*v)["node"].isInt() && (*v)["index"].isInt() && (*v)["value"].isDouble());
                        int node = (*v)["node"].asInt();
                        int index = (*v)["index"].asInt();
                        float value = static_cast<float>((*v)["value"].asDouble());

                        std::function<void ()> f = [] {}; 

                        // finally handle any other UI updates implied by this command
                        if ((*v)["slider"].isObject() && (*v)["slider"] != Json::nullValue) {
                           auto slider = (*v)["slider"];
                           
                           assert(slider["name"].isString());
                           auto name = slider["name"].asString();

                           if (slider["value"].isInt() && slider["value"] != Json::nullValue) {
                              auto value = slider["value"].asInt();

                              for (auto s: mISliders) {
                                 if (s->getName().compare(name) == 0) {
                                    f = [=] { s->setValue(value); };
                                 }
                              }
                           }
                           else if (slider["value"].isDouble() && slider["value"] != Json::nullValue) {
                              auto value = slider["value"].asDouble();

                              std::function<void ()> f = [] {}; 
                              for (auto s: mFSliders) {
                                 if (s->getName().compare(name) == 0) {
                                    f = [=] { s->setValue(static_cast<float>(value)); };
                                 }
                              }
                           }
                        }

                        valuesR.push_back(std::make_tuple(node, index, value, f));
                     }
                  }

                  paramsR.push_back(std::pair<std::string, std::vector<std::tuple<int, int, float, std::function<void ()>>>>{name, valuesR});
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

       if (mJSONUi["widgets"]["global_midi"].isArray() && mJSONUi["widgets"]["global_midi"] != Json::nullValue) {
         auto global_midi = mJSONUi["widgets"]["global_midi"];

         for (auto b = global_midi.begin(); b != global_midi.end(); b++) {
            if ((*b)["midi_toggle"].isObject() && (*b)["midi_toggle"] != Json::nullValue) {
               auto toggle = (*b)["midi_toggle"];
               if (toggle["name"].compare("record") == 0) {
                  assert(toggle["node"].isInt() && toggle["index"].isInt());
                  int node = toggle["node"].asInt();
                  int index = toggle["index"].asInt();

                  mRecSysEx.push_back(std::function<void (uint8_t)>([=](uint8_t r) { 
                     if (r == KEYSTEP_PRO_RECORD_OFF_ON) {
                        set_param_float(aaModule, node, index, 1.0);
                     }
                     else {
                        set_param_float(aaModule, node, index, 0.0);
                     }
                  }));
               }
            }

            if ((*b)["midi_off"].isObject() && (*b)["midi_off"] != Json::nullValue) {
               auto midi_off = (*b)["midi_off"];
               if (midi_off["name"].compare("stop") == 0) {
                  assert(midi_off["node"].isInt() && midi_off["index"].isInt());
                  int node = midi_off["node"].asInt();
                  int index = midi_off["index"].asInt();

                  mStopSysEx.push_back(std::function<void ()>([=]() { 
                     set_param_float(aaModule, node, index, 0.0);
                  }));
               }
            }

            if ((*b)["midi_on"].isObject() && (*b)["midi_on"] != Json::nullValue) {
               auto midi_on = (*b)["midi_on"];
               if (midi_on["name"].compare("play") == 0) {
                  assert(midi_on["node"].isInt() && midi_on["index"].isInt());
                  int node = midi_on["node"].asInt();
                  int index = midi_on["index"].asInt();

                  mPlaySysEx.push_back(std::function<void (uint8_t)>([=](uint8_t r) { 
                     if (r == KEYSTEP_PRO_PLAY_OFF_ON) {
                        set_param_float(aaModule, node, index, 1.0);    
                     }
                     else {
                        // KEYSTEP_PRO_PLAY_PAUSE
                     }
                  }));
               }
            }
         }
       }

      if (mJSONUi["widgets"]["checkboxes"].isArray() && mJSONUi["widgets"]["checkboxes"] != Json::nullValue) {
         auto checkboxes = mJSONUi["widgets"]["checkboxes"];

         for (auto b = checkboxes.begin(); b != checkboxes.end(); b++) {
            assert((*b)["label"].isString());
            auto label = (*b)["label"].asString();

            assert((*b)["init"].isBool());
            bool init = (*b)["init"].asBool();
            
            assert((*b)["node"].isInt() && (*b)["index"].isInt());
            int node = (*b)["node"].asInt();
            int index = (*b)["index"].asInt();

            if ((*b)["anchored"].isString()) {
               auto anchor = toAnchorDirection((*b)["anchored"].asString());
               if (mAACheckboxes.size() > 0) {
                  auto checkbox = new AACheckbox(
                     node, index,
                     this, label.c_str(), mAACheckboxes[mAACheckboxes.size()-1]->getCheckbox(), anchor, init);
                  mAACheckboxes.push_back(checkbox);
                  // finally init param in module
                  checkbox->setParam(aaModule);

                  // check midi
                  if ((*b)["midi_toggle"].isObject() && (*b)["midi_toggle"] != Json::nullValue) {
                     auto toggle = (*b)["midi_toggle"];
                     if (toggle["name"].compare("record") == 0) {
                        if (toggle["node"].isInt() && toggle["index"].isInt()) {
                           int node = toggle["node"].asInt();
                           int index = toggle["index"].asInt();

                           mRecSysEx.push_back(std::function<void (uint8_t)>([=](uint8_t r) { 
                              if (r == KEYSTEP_PRO_RECORD_OFF_ON) {
                                 checkbox->setValue(true); // we do not send to WASM as action seperated
                                 set_param_float(aaModule, node, index, 1.0);
                              }
                              else {
                                 checkbox->setValue(false);     
                                 set_param_float(aaModule, node, index, 0.0);
                              }
                           }));
                        }
                        else {
                           mRecSysEx.push_back(std::function<void (uint8_t)>([=](uint8_t r) { 
                              checkbox->setValue(r == KEYSTEP_PRO_RECORD_OFF_ON);
                           }));
                        }
                     }
                  }

                  if ((*b)["midi_off"].isObject() && (*b)["midi_off"] != Json::nullValue) {
                     auto midi_off = (*b)["midi_off"];
                     if (midi_off["name"].compare("stop") == 0) {
                        if (midi_off["node"].isInt() && midi_off["index"].isInt()) {
                           int node = midi_off["node"].asInt();
                           int index = midi_off["index"].asInt();

                           mStopSysEx.push_back(std::function<void ()>([=]() { 
                              checkbox->setValue(false);     
                              set_param_float(aaModule, node, index, 0.0);
                           }));
                        }
                        else {
                           mStopSysEx.push_back(std::function<void ()>([=]() { 
                              checkbox->setValue(false);     
                           }));
                        }
                     }
                  }

                  if ((*b)["midi_on"].isObject() && (*b)["midi_on"] != Json::nullValue) {
                     auto midi_on = (*b)["midi_on"];
                     if (midi_on["name"].compare("play") == 0) {
                        if (midi_on["node"].isInt() && midi_on["index"].isInt()) {
                           int node = midi_on["node"].asInt();
                           int index = midi_on["index"].asInt();

                           mPlaySysEx.push_back(std::function<void (uint8_t)>([=](uint8_t r) { 
                              if (r == KEYSTEP_PRO_PLAY_OFF_ON) {
                                 checkbox->setValue(true); 
                                 set_param_float(aaModule, node, index, 1.0);    
                              }
                              else {
                                 // KEYSTEP_PRO_PLAY_PAUSE
                              }
                           }));
                        }
                        else {
                           mPlaySysEx.push_back(std::function<void (uint8_t)>([=](uint8_t r) { 
                              if (r == KEYSTEP_PRO_PLAY_OFF_ON) {
                                 checkbox->setValue(true);  
                              }
                              else {
                                 // KEYSTEP_PRO_PLAY_PAUSE
                              }
                           }));
                        }
                     }
                  }
               }
            }
            else {
               assert((*b)["x"].isInt() && (*b)["y"].isInt());
               int x = (*b)["x"].asInt();
               int y = (*b)["y"].asInt();
               auto checkbox = new AACheckbox(
                     node, index,
                     this, label.c_str(), x, y, init);
               mAACheckboxes.push_back(checkbox);
               // finally init param in module
               checkbox->setParam(aaModule);

               // check midi
               if ((*b)["midi_toggle"].isObject() && (*b)["midi_toggle"] != Json::nullValue) {
                  auto toggle = (*b)["midi_toggle"];
                  if (toggle["name"].compare("record") == 0) {
                     if (toggle["node"].isInt() && toggle["index"].isInt()) {
                        int node = toggle["node"].asInt();
                        int index = toggle["index"].asInt();

                        mRecSysEx.push_back(std::function<void (uint8_t)>([=](uint8_t r) { 
                           if (r == KEYSTEP_PRO_RECORD_OFF_ON) {
                              checkbox->setValue(true); // we do not send to WASM as action seperated
                              set_param_float(aaModule, node, index, 1.0);
                           }
                           else {
                              checkbox->setValue(false);     
                              set_param_float(aaModule, node, index, 0.0);
                           }
                        }));
                     }
                     else {
                        mRecSysEx.push_back(std::function<void (uint8_t)>([=](uint8_t r) { 
                           checkbox->setValue(r == KEYSTEP_PRO_RECORD_OFF_ON);
                        }));
                     }
                  }
               }

               if ((*b)["midi_off"].isObject() && (*b)["midi_off"] != Json::nullValue) {
                  auto midi_off = (*b)["midi_off"];
                  if (midi_off["name"].compare("stop") == 0) {
                     if (midi_off["node"].isInt() && midi_off["index"].isInt()) {
                        int node = midi_off["node"].asInt();
                        int index = midi_off["index"].asInt();

                        mStopSysEx.push_back(std::function<void ()>([=]() { 
                           checkbox->setValue(false);     
                           set_param_float(aaModule, node, index, 0.0);
                        }));
                     }
                     else {
                        mStopSysEx.push_back(std::function<void ()>([=]() { 
                           checkbox->setValue(false);     
                        }));
                     }
                  }
               }

               if ((*b)["midi_on"].isObject() && (*b)["midi_on"] != Json::nullValue) {
                   std::cout << "midi_on" << std::endl;
                  auto midi_on = (*b)["midi_on"];
                  if (midi_on["name"].compare("play") == 0) {
                     if (midi_on["node"].isInt() && midi_on["index"].isInt()) {
                        int node = midi_on["node"].asInt();
                        int index = midi_on["index"].asInt();

                        mPlaySysEx.push_back(std::function<void (uint8_t)>([=](uint8_t r) { 
                           if (r == KEYSTEP_PRO_PLAY_OFF_ON) {
                              checkbox->setValue(true); 
                              set_param_float(aaModule, node, index, 1.0);    
                           }
                           else {
                              // KEYSTEP_PRO_PLAY_PAUSE
                           }
                        }));
                     }
                     else {
                        mPlaySysEx.push_back(std::function<void (uint8_t)>([=](uint8_t r) { 
                           if (r == KEYSTEP_PRO_PLAY_OFF_ON) {
                              checkbox->setValue(true); 
                           }
                           else {
                              // KEYSTEP_PRO_PLAY_PAUSE
                           }
                        }));
                     }
                  }
               }
            }
         }
      }

      if (mJSONUi["widgets"]["radio_buttons"].isArray()) {
         auto radiobuttons = mJSONUi["widgets"]["radio_buttons"];

         for (auto b = radiobuttons.begin(); b != radiobuttons.end(); b++) {
            assert((*b)["name"].isString() && (*b)["direction"].isString());
            auto name = (*b)["name"].asString();
            auto direction = toRadioDirection((*b)["direction"].asString());

            std::vector<std::pair<std::string, std::function<void ()>>> paramsR;

            if ((*b)["buttons"].isArray()) {
               auto params = (*b)["buttons"];

               for (auto p = params.begin(); p != params.end(); p++) {
                  assert((*p)["label"].isString());
                  auto name = (*p)["label"].asString();

                  assert((*p)["node"].isInt() && (*p)["index"].isInt() && (*p)["value"].isDouble());
                  int node = (*p)["node"].asInt();
                  int index = (*p)["index"].asInt();
                  float value = static_cast<float>((*p)["value"].asDouble());

                  auto f = std::function<void ()>([=]() {
                     set_param_float(aaModule, node, index, value);
                  });
                 
                  paramsR.push_back(std::pair<std::string, std::function<void ()>>{name, f});
               }
            }

            if ((*b)["anchored"].isString()) {
               auto anchor = toAnchorDirection((*b)["anchored"].asString());
               if (mAARadioButtons.size() > 0) {
                  auto radiobutton = new AARadioButton(
                     paramsR, this, name.c_str(), 
                     mAARadioButtons[mAARadioButtons.size()-1]->getRadioButton(), 
                     anchor,
                     direction);
                  mAARadioButtons.push_back(radiobutton);
               }
            }
            else {
               assert((*b)["x"].isInt() && (*b)["y"].isInt());
               int x = (*b)["x"].asInt();
               int y = (*b)["y"].asInt();
               auto radiobutton = new AARadioButton(paramsR, this, name.c_str(), x, y, direction);
               mAARadioButtons.push_back(radiobutton);
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

   // process audio in AA world
   mModuleCompute(bufferSize, out);
   
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

   // draw checkboxes
   for (auto c: mAACheckboxes) {
      c->draw();
   }

   // draw radio buttons
   for (auto r: mAARadioButtons) {
      r->draw();
   }

   DrawLogo(115, 70, 0.25, 0.25);
}

void AATest::DrawLogo(float  x,  float y, float scaleX, float scaleY) 
{
   nvgSave(gNanoVG);
   nvgBeginPath(gNanoVG);

   nvgStrokeColor(gNanoVG, nvgRGBA(243,184,60,255));
   nvgStrokeWidth(gNanoVG, 8.0);

   nvgTranslate(gNanoVG, x, y);
   nvgScale(gNanoVG, scaleX, scaleY);
   
//   M27 10.7  v200
   nvgMoveTo(gNanoVG, 27, 10.7);
   nvgLineTo(gNanoVG, 27, 210.7);

//   M27 10.7  l100 200
   nvgMoveTo(gNanoVG, 27, 10.7);
   nvgLineTo(gNanoVG, 127, 210.7);

//   M27 10.7  l200 200
   nvgMoveTo(gNanoVG, 27, 10.7);
   nvgLineTo(gNanoVG, 227, 210.7);

//   M27 10.7  c82.8 0 150 22.4 150 50
   nvgMoveTo(gNanoVG, 27, 10.7);
   nvgBezierTo(gNanoVG, 82.8+27, 10.7, 150+27, 22.4+10.7, 150+27, 50+10.7);

//   M177 60.7 c27.6 0 50 67.2 50 150 l0 0
   nvgMoveTo(gNanoVG, 177, 60.7);
   nvgBezierTo(gNanoVG, 27.6+177, 0+60.7, 50+177, 67.2+60.7, 50+177, 150+60.7);

//   M97 140.7 l30-30
   nvgMoveTo(gNanoVG, 97, 140.7);
   nvgLineTo(gNanoVG, 30+97, -30+140.7);

//   M97 140.7 c-20 26.7-43.3 36.7-70 30
   nvgMoveTo(gNanoVG, 97, 140.7);
   nvgBezierTo(gNanoVG, -20 + 97, 26.7 + 140.7, -43.3 + 97, 36.7 + 140.7, -70 + 97, 30 + 140.7);

   // M177,60.7l20-20
   nvgMoveTo(gNanoVG, 177, 60.7);
   nvgLineTo(gNanoVG, 197, 40.7);

   nvgStroke(gNanoVG);
   nvgRestore(gNanoVG);
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
   mModuleSaveData.LoadString("aamodule", moduleInfo);
   mModuleSaveData.LoadString("target", moduleInfo);

   SetUpFromSaveData();
}

void AATest::SetUpFromSaveData()
{
   string name = mModuleSaveData.GetString("aamodule");
   if (name != "") 
   {
      SetModule(name);
   }

   SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("target")));
}

void AATest::DropdownUpdated(DropdownList* list, int oldVal)
{
   for (auto dropdown: mAADropdownLists) {
      dropdown->updated(aaModule, list);
   }
}

void AATest::RadioButtonUpdated(RadioButton* radiobutton, int oldVal)
{
   for (auto r: mAARadioButtons) {
      r->updated(aaModule, radiobutton);
   }
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
   for (auto c: mAACheckboxes) {
      if (c->isCheckbox(checkbox)) {
         c->setParam(aaModule);
      }
   }
}

void AATest::SetModule(string moduleName) {
   ofLog() << "loading AA Module: " << moduleName;

   mModuleSaveData.SetString("aamodule", moduleName);

   // mark module as used
   {
      ofxJSONElement root;
      root.open(ofToDataPath("internal/used_aamodules.json"));
      
      Time time = Time::getCurrentTime();
      root["aamodules"][moduleName] = (double)time.currentTimeMillis();

      root.save(ofToDataPath("internal/used_aamodules.json"), true);
   }

   if  (aaModule != nullptr) {
      return; //  already loaded
   }

   LoadModule(moduleName);
}

void AATest::LoadModule(string moduleName) {

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
         
         // install audio compute function 
         if (mNumInputs > 0) {
            if (mNumInputs == 2) {
               if (mNumOutputs == 2) {
                  // two in two out
                  mModuleCompute = [&](int bufferSize, ChannelBuffer* out) {
                     aa_module_compute_two_two_non(
                        aaModule, 
                        bufferSize, 
                        GetBuffer()->GetChannel(0), GetBuffer()->GetChannel(1),  
                        out->GetChannel(0), out->GetChannel(1));
                  };
               }
               else {
                  // two in one out
                  //TODO: implement two in one out compute
               }
            }
            else { // one input 
               if (mNumOutputs == 2) {
                  // one in two out
                  mModuleCompute = [&] (int bufferSize, ChannelBuffer* out) {
                     aa_module_compute_one_two_non(
                        aaModule, 
                        bufferSize, 
                        GetBuffer()->GetChannel(0), 
                        out->GetChannel(0), out->GetChannel(1));
                  };
               }
               else {
                  // one in one out
                  mModuleCompute = [&] (int bufferSize, ChannelBuffer* out) {
                     aa_module_compute_one_one(
                        aaModule, 
                        bufferSize, 
                        GetBuffer()->GetChannel(0), 
                        out->GetChannel(0));
                  };
               }
            }
         }
         else if (mNumOutputs == 2) {
            // zero in one two out
            mModuleCompute = [&] (int bufferSize, ChannelBuffer* out) {
               aa_module_compute_zero_two_non(aaModule, bufferSize, out->GetChannel(0), out->GetChannel(1));
            };
         } 
         else {
            // zero in one out
            mModuleCompute = [&] (int bufferSize, ChannelBuffer* out) {
               aa_module_compute_zero_one(aaModule, bufferSize, out->GetChannel(0));
            };
         }

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

namespace {
   
}

// Note: Below is explict support for the KeyStep Pro SysEx messages
//       This does not effect portablity, but it does mean that without 
//       KeyStep Pro it is not currently possible to take advantage of 
//       Stop, Rec, and Play messages.
// TODO: Find a more generic approach to handling this, in particular, 
//       define a .json format for controllers incoming SysEx messages with 
//       a mapping.
void AATest::SendMidi(const MidiMessage& message) {
   //std::cout << message.getDescription() << std::endl;
   if (message.isSysEx()) { 
      // handle KeyStep Pro SysEx Messages
      if (message.getSysExDataSize () == KEYSTEP_PRO_SYSEX_MSG_SIZE) {
         auto bytes = message.getSysExData();
         //std::cout << message.getDescription() << std::endl;
         // is it a message from KeyStep ?
         if (bytes[0] == 0x7F && bytes[1] == 0x7f && bytes[2] == 0x06) {
            switch (bytes[3]) {
               case KEYSTEP_PRO_RECORD_OFF_ON: {
                  for (auto r: mRecSysEx) {
                     r(KEYSTEP_PRO_RECORD_OFF_ON);
                  }
                  break;
               }
               case KEYSTEP_PRO_RECORD_ON_OFF: {
                  for (auto r: mRecSysEx) {
                     r(KEYSTEP_PRO_RECORD_ON_OFF);
                  }
                  break;
               }
               case KEYSTEP_PRO_STOP: {
                  for (auto r: mStopSysEx) {
                     r();
                  }
                  break;
               }
               case KEYSTEP_PRO_PLAY_OFF_ON: {
                  for (auto r: mPlaySysEx) {
                     r(KEYSTEP_PRO_PLAY_OFF_ON);
                  }
                  break;
               }
               case KEYSTEP_PRO_PLAY_PAUSE: {
                  for (auto r: mPlaySysEx) {
                     r(KEYSTEP_PRO_PLAY_PAUSE);
                  }
                  break;
               }
            }
         }
      }
   }
   else if (message.isController()) {
      // TODO: add CC feature control
   }
}