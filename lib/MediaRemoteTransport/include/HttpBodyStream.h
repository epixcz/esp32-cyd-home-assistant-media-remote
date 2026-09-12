#pragma once
#include <MediaRemoteCore.h>
#include <algorithm>

namespace media_remote {
template<class StreamBase, class Source, class Http, class Clock>
class HttpBodyStream : public StreamBase {
public:
  HttpBodyStream(
    Source *source, Http *http, bool chunked, int expectedLength,
    size_t maxBytes, uint32_t totalTimeoutMs, uint32_t idleTimeoutMs, uint32_t startedAt)
    : source_(source), http_(http), chunked_(chunked), expectedLength_(expectedLength),
      maxBytes_(maxBytes), totalTimeoutMs_(totalTimeoutMs), idleTimeoutMs_(idleTimeoutMs),
      startedAt_(startedAt), lastByteAt_(Clock::now())
  {
  }

  int available() override
  {
    return result_ == media_remote::InputResult::Ok && !finished_ && source_
      ? source_->available()
      : 0;
  }

  int read() override
  {
    uint8_t value;
    return readBytes(&value, 1) == 1 ? value : -1;
  }

  int peek() override
  {
    return -1;
  }

  void flush() override
  {
    if (source_) {
      source_->flush();
    }
  }

  size_t write(uint8_t) override
  {
    return 0;
  }

  size_t readBytes(char *buffer, size_t length) override
  {
    return readBytes(reinterpret_cast<uint8_t *>(buffer), length);
  }

  size_t readBytes(uint8_t *buffer, size_t length) override
  {
    if (!buffer || length == 0 || result_ != media_remote::InputResult::Ok || finished_) {
      return 0;
    }

    size_t done = 0;
    while (done < length && result_ == media_remote::InputResult::Ok && !finished_) {
      if (chunked_ && chunkRemaining_ == 0 && !prepareNextChunk()) {
        break;
      }
      if (!chunked_ && expectedLength_ >= 0 && bytesRead_ >= static_cast<size_t>(expectedLength_)) {
        finished_ = true;
        break;
      }
      if (!chunked_ && expectedLength_ < 0 && bytesRead_ >= maxBytes_) {
        if (source_->available() > 0) {
          result_ = media_remote::InputResult::TooLarge;
          break;
        }
        if (!http_->connected()) {
          finished_ = true;
          break;
        }
        media_remote::InputResult timing = media_remote::checkInputBudget(
          Clock::now(), startedAt_, lastByteAt_, bytesRead_, SIZE_MAX, totalTimeoutMs_, idleTimeoutMs_);
        if (timing == media_remote::InputResult::Timeout) {
          result_ = timing;
          break;
        }
        Clock::sleep(1);
        continue;
      }
      if (!chunked_ && expectedLength_ < 0 && source_->available() == 0 && !http_->connected()) {
        finished_ = true;
        break;
      }

      media_remote::InputResult budget = media_remote::checkInputBudget(
        Clock::now(), startedAt_, lastByteAt_, bytesRead_, maxBytes_, totalTimeoutMs_, idleTimeoutMs_);
      if (budget != media_remote::InputResult::Ok) {
        result_ = budget;
        break;
      }

      size_t allowed = std::min(length - done, maxBytes_ - bytesRead_);
      if (chunked_) {
        allowed = std::min(allowed, chunkRemaining_);
      } else if (expectedLength_ >= 0) {
        allowed = std::min(allowed, static_cast<size_t>(expectedLength_) - bytesRead_);
      }
      if (allowed == 0) {
        result_ = media_remote::InputResult::TooLarge;
        break;
      }

      int availableBytes = source_->available();
      if (availableBytes <= 0) {
        if (!http_->connected()) {
          result_ = media_remote::InputResult::TransportError;
          break;
        }
        Clock::sleep(1);
        continue;
      }

      size_t requested = std::min(allowed, static_cast<size_t>(availableBytes));
      int count = source_->read(buffer + done, requested);
      if (count <= 0) {
        Clock::sleep(1);
        continue;
      }
      done += static_cast<size_t>(count);
      bytesRead_ += static_cast<size_t>(count);
      lastByteAt_ = Clock::now();
      if (chunked_) {
        chunkRemaining_ -= static_cast<size_t>(count);
      }
    }
    return done;
  }

  media_remote::InputResult result() const { return result_; }
  size_t bytesRead() const { return bytesRead_; }

private:
  bool readRawByte(uint8_t *value)
  {
    while (result_ == media_remote::InputResult::Ok) {
      media_remote::InputResult timing = media_remote::checkInputBudget(
        Clock::now(), startedAt_, lastByteAt_, bytesRead_, SIZE_MAX, totalTimeoutMs_, idleTimeoutMs_);
      if (timing == media_remote::InputResult::Timeout) {
        result_ = timing;
        return false;
      }
      if (source_->available() > 0) {
        int readValue = source_->read();
        if (readValue >= 0) {
          *value = static_cast<uint8_t>(readValue);
          lastByteAt_ = Clock::now();
          return true;
        }
      } else if (!http_->connected()) {
        result_ = media_remote::InputResult::TransportError;
        return false;
      }
      Clock::sleep(1);
    }
    return false;
  }

  bool prepareNextChunk()
  {
    if (finished_) {
      return false;
    }
    if (haveChunkData_) {
      uint8_t carriageReturn;
      uint8_t lineFeed;
      if (!readRawByte(&carriageReturn) || !readRawByte(&lineFeed)) return false;
      if (carriageReturn != '\r' || lineFeed != '\n') {
        result_ = media_remote::InputResult::TransportError;
        return false;
      }
      haveChunkData_ = false;
    }

    size_t chunkSize = 0;
    bool haveDigit = false;
    bool extension = false;
    size_t headerLength = 0;
    while (headerLength++ < 32) {
      uint8_t value;
      if (!readRawByte(&value)) {
        return false;
      }
      if (value == '\r') {
        if (!readRawByte(&value)) return false;
        if (value != '\n' || !haveDigit) {
          result_ = media_remote::InputResult::TransportError;
          return false;
        }
        if (chunkSize == 0) {
          finished_ = true;
          return false;
        }
        if (chunkSize > maxBytes_ - bytesRead_) {
          result_ = media_remote::InputResult::TooLarge;
          return false;
        }
        chunkRemaining_ = chunkSize;
        haveChunkData_ = true;
        return true;
      }
      if (extension) {
        continue;
      }
      if (value == ';') {
        extension = true;
        continue;
      }
      int digit = value >= '0' && value <= '9' ? value - '0'
        : value >= 'a' && value <= 'f' ? value - 'a' + 10
        : value >= 'A' && value <= 'F' ? value - 'A' + 10
        : -1;
      if (digit < 0 || chunkSize > (SIZE_MAX - static_cast<size_t>(digit)) / 16) {
        result_ = media_remote::InputResult::TransportError;
        return false;
      }
      haveDigit = true;
      chunkSize = chunkSize * 16 + static_cast<size_t>(digit);
    }
    result_ = media_remote::InputResult::TransportError;
    return false;
  }

  Source *source_;
  Http *http_;
  bool chunked_;
  int expectedLength_;
  size_t maxBytes_;
  uint32_t totalTimeoutMs_;
  uint32_t idleTimeoutMs_;
  uint32_t startedAt_;
  uint32_t lastByteAt_;
  size_t bytesRead_ = 0;
  size_t chunkRemaining_ = 0;
  bool haveChunkData_ = false;
  bool finished_ = false;
  media_remote::InputResult result_ = media_remote::InputResult::Ok;
};

} // namespace media_remote
