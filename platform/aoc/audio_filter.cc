/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "chre/platform/aoc/audio_filter.h"
#include "chre/core/event_loop_manager.h"
#include "chre/platform/aoc/memory.h"
#include "chre/platform/assert.h"
#include "chre/platform/log.h"
#include "chre/platform/system_time.h"
#include "chre/util/time.h"
#include "efw/include/ring_buffer_ipc.h"

namespace chre {

AudioFilter::AudioFilter()
    : Filter("CHRE_FLT" /* name */, EFWObject::Root() /* parent */,
             tskIDLE_PRIORITY + 5 /* priority */, 512 /* stack size words */,
             1 /* command depth */, 1 /* Num Filter inputs */,
             1 /* Num Filter Outputs */, 1 /* Num Ring Buffers */) {
  LOGV("Filter Created");

  TaskSpawn();
}

int AudioFilter::StartCallback() {
  mRingBufferHandle = ring_[kRingIndex]->ReaderRegister();
  CHRE_ASSERT(mRingBufferHandle != nullptr);

  // TODO (b/168330113): The sample buffer is a fairly large chunk of
  // contiguous memory that may be prone to allocation failures - reserve
  // this memory statically.
  forceDramAccess();
  mSampleBufferDram =
      static_cast<int16_t *>(memoryAllocDram(sizeof(mSampleBufferSram)));
  CHRE_ASSERT(mSampleBufferDram != nullptr);

  return 0;
}

bool AudioFilter::StopCallback() {
  ring_[kRingIndex]->ReaderUnregister(mRingBufferHandle);
  mRingBufferHandle = nullptr;

  forceDramAccess();
  memoryFreeDram(mSampleBufferDram);
  mSampleBufferDram = nullptr;

  return true;
}

bool AudioFilter::SignalProcessor(int index) {
  switch (index) {
    case kBufferReleasedSignalIndex:
      OnBufferReleased();
      break;
    default:
      break;
  }
  return true;
}

void AudioFilter::InputProcessor(int /*pin*/, void *message, size_t size) {
  CHRE_ASSERT(message != nullptr);

  // TODO check metadata format etc.
  auto *metadata = static_cast<struct AudioInputMetadata *>(message);

  constexpr size_t kBytesPerAocSample = sizeof(uint32_t);
  constexpr size_t kNumSamplesAoc = 160;
  constexpr size_t kRingSize = kNumSamplesAoc * kBytesPerAocSample *
                               1;  // 1 channel of u32 samples per 10 ms
  uint32_t buffer[kNumSamplesAoc];

  if (metadata->need_resync) {
    ring_[kRingIndex]->ReaderSyncOffset(mRingBufferHandle, kBytesPerAocSample);
  }

  uint32_t nBytes =
      ring_[kRingIndex]->Read(mRingBufferHandle, &buffer, kRingSize);
  CHRE_ASSERT_LOG(nBytes != 0, "Got data pipe notif, but no data in ring");
  size_t nSamples = nBytes / kBytesPerAocSample;

  // TODO: For the initial implementation/testing, we assume that the
  // frame might not fit to a T in our current buffer, but that is the
  // expectation. With the current logic, we could end up dropping a frame,
  // revisit this to make sure we capture the exact number of samples,
  // while continuing to buffer samples either via a ping-pong scheme, or
  // a small extra scratch space.
  if ((nSamples + mSampleCount) > kSamplesPer2Sec) {
    if (!mDramBufferInUse) {
      forceDramAccess();
      memcpy(mSampleBufferDram, mSampleBufferSram, sizeof(mSampleBufferSram));
      mDataEvent.sampleCount = mSampleCount;
      mDataEvent.samplesS16 = mSampleBufferDram;
      mDataEvent.timestamp = mFrameStartTimestamp;
      mDramBufferInUse = true;

      EventLoopManagerSingleton::get()
          ->getAudioRequestManager()
          .handleAudioDataEvent(&mDataEvent);
    } else {
      LOGW("SRAM audio buffer full while DRAM audio buffer not released!");
    }
    mSampleCount = 0;
  }

  if (mSampleCount == 0) {
    // TODO: Get a better estimate of the timestamp of the first sample in
    // the frame. Since the pipe notification arrives every 10ms, we could
    // subtract 10ms from now(), and maybe even keep track of the embedded
    // timestamp of the first sample of the current frame, and the last
    // sample of the previous frame.
    mFrameStartTimestamp = SystemTime::getMonotonicTime().toRawNanoseconds();
  }

  for (size_t i = 0; i < nSamples; ++i, ++mSampleCount) {
    // The zeroth byte of the received sample is a timestamp, which we
    // strip out.
    // TODO: Verify that there's no scaling on the remaining data, in
    // which case casting without descaling won't be the right thing to
    // do.
    mSampleBufferSram[mSampleCount] =
        static_cast<int16_t>((buffer[i] >> 8) & 0xffff);
  }
}

void AudioFilter::OnBufferReleased() {
  mDramBufferInUse = false;
}

}  // namespace chre
