// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "core/internal/internal_payload_factory.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

#include "file/util/temp_path.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "core/internal/offline_frames.h"
#include "platform/base/byte_array.h"
#include "platform/public/pipe.h"
#include "proto/connections/offline_wire_formats.pb.h"

namespace location {
namespace nearby {
namespace connections {
namespace {

constexpr char kText[] = "data chunk";
#define TEST_FILE_NAME std::string("testfilename.txt")
#define TEST_FILE_PARENT_FOLDER std::string("")

class InternalPayloadFActoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    temp_path_ = std::make_unique<TempPath>(TempPath::Local);
    path_ = temp_path_->path() + "/" + TEST_FILE_NAME;
    file_ = std::fstream(path_, std::fstream::out | std::fstream::trunc);
    file_ << "This is a test file with a minimum of 101 characters. This is "
             "used to verify the InputFile in the payload_test google test.";
    file_.close();
  }

  void TearDown() override { std::remove(path_.c_str()); }

  std::fstream file_;
  std::unique_ptr<TempPath> temp_path_;
  std::string path_;
};

TEST_F(InternalPayloadFActoryTest, CanCreateIternalPayloadFromBytePayload) {
  ByteArray data(kText);
  std::unique_ptr<InternalPayload> internal_payload =
      CreateOutgoingInternalPayload(Payload{data});
  EXPECT_NE(internal_payload, nullptr);
  Payload payload = internal_payload->ReleasePayload();
  EXPECT_EQ(payload.AsFile(), nullptr);
  EXPECT_EQ(payload.AsStream(), nullptr);
  EXPECT_EQ(payload.AsBytes(), ByteArray(kText));
}

TEST_F(InternalPayloadFActoryTest, CanCreateIternalPayloadFromStreamPayload) {
  auto pipe = std::make_shared<Pipe>();
  std::unique_ptr<InternalPayload> internal_payload =
      CreateOutgoingInternalPayload(Payload{[pipe]() -> InputStream& {
        return pipe->GetInputStream();  // NOLINT
      }});
  EXPECT_NE(internal_payload, nullptr);
  Payload payload = internal_payload->ReleasePayload();
  EXPECT_EQ(payload.AsFile(), nullptr);
  EXPECT_NE(payload.AsStream(), nullptr);
  EXPECT_EQ(payload.AsBytes(), ByteArray());
}

TEST_F(InternalPayloadFActoryTest, CanCreateIternalPayloadFromFilePayload) {
  std::unique_ptr<InternalPayload> internal_payload =
      CreateOutgoingInternalPayload(Payload{
          path_.c_str(), TEST_FILE_NAME.c_str(), InputFile(path_.c_str())});
  EXPECT_NE(internal_payload, nullptr);
  Payload payload = internal_payload->ReleasePayload();
  EXPECT_NE(payload.AsFile(), nullptr);
  EXPECT_EQ(payload.AsStream(), nullptr);
  EXPECT_EQ(payload.AsBytes(), ByteArray());
  EXPECT_EQ(payload.AsFile()->GetFilePath(), path_);
  payload.AsFile()->Close();
  std::filesystem::remove(path_);
}

TEST_F(InternalPayloadFActoryTest, CanCreateIternalPayloadFromByteMessage) {
  PayloadTransferFrame frame;
  frame.set_packet_type(PayloadTransferFrame::DATA);
  std::int64_t payload_chunk_offset = 0;
  ByteArray data(kText);
  PayloadTransferFrame::PayloadChunk payload_chunk;
  payload_chunk.set_offset(payload_chunk_offset);
  payload_chunk.set_body(std::string(std::move(data)));
  payload_chunk.set_flags(0);
  auto& header = *frame.mutable_payload_header();
  header.set_type(PayloadTransferFrame::PayloadHeader::BYTES);
  header.set_id(12345);
  header.set_total_size(512);
  *frame.mutable_payload_chunk() = std::move(payload_chunk);
  std::unique_ptr<InternalPayload> internal_payload =
      CreateIncomingInternalPayload(frame);
  EXPECT_NE(internal_payload, nullptr);
  Payload payload = internal_payload->ReleasePayload();
  EXPECT_EQ(payload.AsFile(), nullptr);
  EXPECT_EQ(payload.AsStream(), nullptr);
  EXPECT_EQ(payload.AsBytes(), ByteArray(kText));
}

TEST_F(InternalPayloadFActoryTest, CanCreateIternalPayloadFromStreamMessage) {
  PayloadTransferFrame frame;
  frame.set_packet_type(PayloadTransferFrame::DATA);
  auto& header = *frame.mutable_payload_header();
  header.set_type(PayloadTransferFrame::PayloadHeader::STREAM);
  header.set_id(12345);
  header.set_total_size(0);
  std::unique_ptr<InternalPayload> internal_payload =
      CreateIncomingInternalPayload(frame);
  EXPECT_NE(internal_payload, nullptr);
  Payload payload = internal_payload->ReleasePayload();
  EXPECT_EQ(payload.AsFile(), nullptr);
  EXPECT_NE(payload.AsStream(), nullptr);
  EXPECT_EQ(payload.AsBytes(), ByteArray());
  EXPECT_EQ(payload.GetType(), Payload::Type::kStream);
}

TEST_F(InternalPayloadFActoryTest, CanCreateIternalPayloadFromFileMessage) {
  PayloadTransferFrame frame;
  frame.set_packet_type(PayloadTransferFrame::DATA);
  auto& header = *frame.mutable_payload_header();
  header.set_type(PayloadTransferFrame::PayloadHeader::FILE);
  header.set_id(12345);
  header.set_total_size(512);
  std::unique_ptr<InternalPayload> internal_payload =
      CreateIncomingInternalPayload(frame);
  EXPECT_NE(internal_payload, nullptr);
  Payload payload = internal_payload->ReleasePayload();
  EXPECT_NE(payload.AsFile(), nullptr);
  EXPECT_EQ(payload.AsStream(), nullptr);
  EXPECT_EQ(payload.AsBytes(), ByteArray());
  EXPECT_EQ(payload.GetType(), Payload::Type::kFile);
}

void CreateFileWithContents(const char* file_path, const ByteArray& contents) {
  std::unique_ptr<OutputFile> file = std::make_unique<OutputFile>(file_path);
  EXPECT_TRUE(file->Write(contents).Ok());
  EXPECT_TRUE(file->Close().Ok());
}

TEST_F(InternalPayloadFActoryTest,
       SkipToOffset_FilePayloadValidOffset_SkipsOffset) {
  ByteArray contents("0123456789");
  constexpr size_t kOffset = 4;
  size_t size_after_skip = contents.size() - kOffset;
  NEARBY_LOGS(INFO)
      << "SkipToOffset_FilePayloadValidOffset_SkipsOffset: file path = "
      << path_.c_str() << "\n";
  NEARBY_LOGS(INFO)
      << "SkipToOffset_FilePayloadValidOffset_SkipsOffset: contents = "
      << contents.data() << "\n";

  CreateFileWithContents(path_.c_str(), contents);
  Payload::Id payload_id = Payload::GenerateId();
  std::unique_ptr<InputFile> inputFile =
      std::make_unique<InputFile>(path_.c_str());
  std::unique_ptr<InternalPayload> internal_payload =
      CreateOutgoingInternalPayload(Payload{payload_id, std::move(*inputFile)});
  EXPECT_NE(internal_payload, nullptr);

  ExceptionOr<size_t> result = internal_payload->SkipToOffset(kOffset);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.GetResult(), kOffset);
  EXPECT_EQ(internal_payload->GetTotalSize(), contents.size());
  ByteArray contents_after_skip =
      internal_payload->DetachNextChunk(size_after_skip);
  EXPECT_EQ(contents_after_skip, ByteArray("456789"));
  internal_payload = nullptr;
  std::filesystem::remove(path_);
}

TEST_F(InternalPayloadFActoryTest,
       SkipToOffset_StreamPayloadValidOffset_SkipsOffset) {
  ByteArray contents("0123456789");
  constexpr size_t kOffset = 6;
  auto pipe = std::make_shared<Pipe>();
  std::unique_ptr<InternalPayload> internal_payload =
      CreateOutgoingInternalPayload(Payload{[pipe]() -> InputStream& {
        return pipe->GetInputStream();  // NOLINT
      }});
  EXPECT_NE(internal_payload, nullptr);
  pipe->GetOutputStream().Write(contents);

  ExceptionOr<size_t> result = internal_payload->SkipToOffset(kOffset);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.GetResult(), kOffset);
  EXPECT_EQ(internal_payload->GetTotalSize(), -1);
  ByteArray contents_after_skip = internal_payload->DetachNextChunk(512);
  EXPECT_EQ(contents_after_skip, ByteArray("6789"));
}

}  // namespace
}  // namespace connections
}  // namespace nearby
}  // namespace location
