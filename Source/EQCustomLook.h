/*
  ==============================================================================

    EQCustomLook.h
    Created: 17 May 2026 5:52:32pm
    Author:  huber

  ==============================================================================
*/

#pragma once


#include <JuceHeader.h>
#include <cmath>


namespace EQColours
{
    const juce::Colour background{ 0xff16161a };
    const juce::Colour panel{ 0xff1f1f26 };
    const juce::Colour panelBorder{ 0xff2e2e3a };
    const juce::Colour knobCap{ 0xff2a2a33 };
    const juce::Colour knobCapEdge{ 0xff3a3a46 };
    const juce::Colour knobDot{ 0xffffffff };
    const juce::Colour trackBg{ 0xff111115 };
    const juce::Colour accentLowCut{ 0xff7b5ea7 };   // purple
    const juce::Colour accentHighCut{ 0xff4ea8a8 };   // cyan
    const juce::Colour accentPeak{ 0xffd4854a };   // amber
    const juce::Colour labelText{ 0xffaaaabc };
    const juce::Colour responseGrid{ 0xff252530 };
    const juce::Colour responseLine{ 0xff4ea8a8 };
    const juce::Colour responseFill{ 0x224ea8a8 };

}

class EQCustomLook : public juce::LookAndFeel_V4
{
public:

    EQCustomLook() {

        setColour(juce::ResizableWindow::backgroundColourId, EQColours::background);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxTextColourId, EQColours::labelText);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Label::textColourId, EQColours::labelText);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y,
        int width, int height, float sliderPosProportional,
        float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider) override {


        //variables for circle positioning & colours
        const float xCenter = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
        const float yCenter = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        const float radius = juce::jmin<float>(static_cast<float>(width) / 2.f,
                                              static_cast<float>(height) / 2.f) - 6.f;
        /*const float xRadius = xCenter - radius;
        const float yRadius = xCenter - radius;*/
        const float trackWidth = radius * 0.12f;
        const float capR = radius - trackWidth - 3.0f;
        
        const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        auto accent = juce::Colour::fromString(slider.getProperties().getWithDefault(
            "accentColour", EQColours::accentPeak.toString()).toString()
        );

        using JointType = juce::PathStrokeType::JointStyle;
        using CapStyle = juce::PathStrokeType::EndCapStyle;

        {
            juce::Path track;
            track.addCentredArc(xCenter, yCenter, radius, radius, 0, rotaryStartAngle, rotaryEndAngle, true);
            g.setColour(EQColours::trackBg);
            g.strokePath(track, juce::PathStrokeType(trackWidth, 
                juce::PathStrokeType::JointStyle::curved, juce::PathStrokeType::EndCapStyle::rounded));
        }

        {
            juce::Path filler;
            filler.addCentredArc(xCenter, yCenter, radius, radius, 0.f,
                rotaryStartAngle, angle, true);
            juce::ColourGradient grad(accent.brighter(0.3f), xCenter, yCenter - radius,
                accent.darker(0.2f), xCenter, yCenter + radius, false);
            g.setGradientFill(grad);
            g.strokePath(filler, juce::PathStrokeType(trackWidth,
                JointType::curved, CapStyle::rounded));
        }

        // --- knob cap ---
        {
            // outer edge highlight
            g.setColour(EQColours::knobCapEdge);
            g.fillEllipse(xCenter - capR, yCenter - capR, capR * 2.f, capR * 2.f);

            // inner body gradient
            juce::ColourGradient capGrad(EQColours::knobCap.brighter(0.08f),
                xCenter, yCenter - capR * 0.5f,
                EQColours::knobCap.darker(0.15f),
                xCenter, yCenter + capR, false);
            g.setGradientFill(capGrad);
            g.fillEllipse(xCenter - capR + 1.5f, yCenter - capR + 1.5f,
                (capR - 1.5f) * 2.f, (capR - 1.5f) * 2.f);
        }

        // --- indicator dot ---
        {
            const float dotR = trackWidth * 0.55f;
            const float dotDst = capR - dotR - 2.f;
            const float dx = xCenter + dotDst * std::sin(angle);
            const float dy = yCenter - dotDst * std::cos(angle);
            g.setColour(EQColours::knobDot);
            g.fillEllipse(dx - dotR, dy - dotR, dotR * 2.f, dotR * 2.f);
        }
    }



    juce::Font getLabelFont(juce::Label& label) override {
        juce::ignoreUnused(label);

        auto value = juce::FontOptions("Helvetica Neue", 10.f, juce::Font::plain);
        return juce::Font(value);
    }
};

