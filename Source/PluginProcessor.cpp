/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
DelayPluginAudioProcessor::DelayPluginAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{

    addParameter(mDryWetParameter = new juce::AudioParameterFloat("drywet",
        "Dry Wet",
        0.0,
        1.0,
        0.5));

    addParameter(mFeedbackParameter = new juce::AudioParameterFloat("feedback",
        "Feedback",
        0.01,
        0.98,
        0.5));

    addParameter(mDelayTimeParameter = new juce::AudioParameterFloat("delaytime",
        "Delay Time",
        0.01,
        MAX_DELAY_TIME,
        0.5));
    mDelayTimeSmoothed = 0;
    mCircularBufferLeft = nullptr;
    mCircularBufferRight = nullptr;
    mCircularBufferWriteHead = 0;
    mCirqularBufferLength = 0;
    mDelayTimeInSamples = 0;
    mDelayReadHead = 0;

    mFeedbackLeft = 0;
    mFeedbackRight = 0;

    

}

DelayPluginAudioProcessor::~DelayPluginAudioProcessor()
{
    if (mCircularBufferLeft != nullptr) {
        delete [] mCircularBufferLeft;
        mCircularBufferLeft = nullptr;

    }

    if (mCircularBufferRight != nullptr) {
        delete [] mCircularBufferRight;
        mCircularBufferRight = nullptr;

    }
}

//==============================================================================
const juce::String DelayPluginAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool DelayPluginAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool DelayPluginAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool DelayPluginAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double DelayPluginAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int DelayPluginAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int DelayPluginAudioProcessor::getCurrentProgram()
{
    return 0;
}

void DelayPluginAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String DelayPluginAudioProcessor::getProgramName (int index)
{
    return {};
}

void DelayPluginAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void DelayPluginAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    mDelayTimeInSamples = sampleRate * *mDelayTimeParameter;

    mCirqularBufferLength = sampleRate * MAX_DELAY_TIME;


    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    if (mCircularBufferLeft == nullptr) {
        mCircularBufferLeft = new float[mCirqularBufferLength];
    }
    juce::zeromem(mCircularBufferLeft, mCirqularBufferLength * sizeof(float));

    if (mCircularBufferRight == nullptr) {
        mCircularBufferRight = new float[mCirqularBufferLength];
   
        

    }

    juce::zeromem(mCircularBufferRight, mCirqularBufferLength * sizeof(float));

    mCircularBufferWriteHead = 0;

    mDelayTimeSmoothed = *mDelayTimeParameter;

}

void DelayPluginAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool DelayPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void DelayPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    

    float* leftChanel = buffer.getWritePointer(0);
    float* rightChanel = buffer.getWritePointer(1);


    for (int i = 0; i < buffer.getNumSamples(); i++) {

        mDelayTimeSmoothed = mDelayTimeSmoothed - 0.001 * (mDelayTimeSmoothed - *mDelayTimeParameter);
        mDelayTimeInSamples = getSampleRate() * mDelayTimeSmoothed;

        mCircularBufferLeft[mCircularBufferWriteHead] = leftChanel[i] + mFeedbackLeft;
        mCircularBufferRight[mCircularBufferWriteHead] = rightChanel[i] + mFeedbackRight;

        mDelayReadHead = mCircularBufferWriteHead - mDelayTimeInSamples;

        if (mDelayReadHead < 0) {
            mDelayReadHead += mCirqularBufferLength;
        }

        int readHead_x = (int)mDelayReadHead;
        int readHead_x1 = readHead_x + 1;
        float readHeadFloat = mDelayReadHead - readHead_x;


        if (readHead_x1 >= mCirqularBufferLength) {
            readHead_x1 -= mCirqularBufferLength;
         }



        float  delay_sample_left = lin_interp(mCircularBufferLeft[readHead_x], mCircularBufferLeft[readHead_x1], readHeadFloat);
        float   delay_sample_right = lin_interp(mCircularBufferRight[readHead_x], mCircularBufferRight[readHead_x1], readHeadFloat);

        mFeedbackLeft = delay_sample_left * *mFeedbackParameter;
        mFeedbackRight = delay_sample_right * *mFeedbackParameter;


        mCircularBufferWriteHead++;

        buffer.addSample(0, i, buffer.getSample(0, i) * (1 - *mDryWetParameter) + delay_sample_left * *mDryWetParameter);
        buffer.addSample(1, i, buffer.getSample(1, i) * (1 - *mDryWetParameter) + delay_sample_right * *mDryWetParameter);

        if (mCircularBufferWriteHead >= mCirqularBufferLength) {
            mCircularBufferWriteHead = 0;
        }
    

    
    
    }
}

//==============================================================================
bool DelayPluginAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* DelayPluginAudioProcessor::createEditor()
{
    return new DelayPluginAudioProcessorEditor (*this);
    
}

//==============================================================================
void DelayPluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void DelayPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DelayPluginAudioProcessor();
}
float DelayPluginAudioProcessor::lin_interp(float sample_x, float sample_x1, float inPhase)
{
    return (1 - inPhase) * sample_x + inPhase * sample_x1;
}
