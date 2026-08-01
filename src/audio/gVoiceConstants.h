/*
 * gVoiceConstants.h
 */

#ifndef GVOICECONSTANTS_H_
#define GVOICECONSTANTS_H_

#include <cstddef>


namespace gvoice {

constexpr int SAMPLERATE = 48000;
constexpr int CHANNELS = 1;
constexpr int BITRATE = 24000;
constexpr int FRAME_MILLISECONDS = 20;
constexpr int FRAME_SAMPLES = SAMPLERATE * FRAME_MILLISECONDS / 1000;
constexpr std::size_t NETWORK_MAX_OPUS_BYTES = 256;

}

#endif /* GVOICECONSTANTS_H_ */
