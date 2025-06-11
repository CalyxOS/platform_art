/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "oat_file.h"

#include <dlfcn.h>

#include <string>

#include "common_runtime_test.h"
#include "dexopt_test.h"
#include "gtest/gtest.h"
#include "scoped_thread_state_change-inl.h"
#include "vdex_file.h"

namespace art HIDDEN {

class OatFileTest : public DexoptTest {};

TEST_F(OatFileTest, LoadOat) {
  std::string dex_location = GetScratchDir() + "/LoadOat.jar";

  Copy(GetDexSrc1(), dex_location);
  GenerateOatForTest(dex_location.c_str(), CompilerFilter::kSpeed);

  std::string oat_location;
  std::string error_msg;
  ASSERT_TRUE(OatFileAssistant::DexLocationToOatFilename(
      dex_location, kRuntimeISA, &oat_location, &error_msg))
      << error_msg;
  std::unique_ptr<OatFile> odex_file(OatFile::Open(/*zip_fd=*/-1,
                                                   oat_location,
                                                   oat_location,
                                                   /*executable=*/false,
                                                   /*low_4gb=*/false,
                                                   dex_location,
                                                   &error_msg));
  ASSERT_TRUE(odex_file.get() != nullptr);

  // Check that the vdex file was loaded in the reserved space of odex file.
  EXPECT_EQ(odex_file->GetVdexFile()->Begin(), odex_file->VdexBegin());
}

TEST_F(OatFileTest, ChangingMultiDexUncompressed) {
  std::string dex_location = GetScratchDir() + "/MultiDexUncompressedAligned.jar";

  Copy(GetTestDexFileName("MultiDexUncompressedAligned"), dex_location);
  GenerateOatForTest(dex_location.c_str(), CompilerFilter::kVerify);

  std::string oat_location;
  std::string error_msg;
  ASSERT_TRUE(OatFileAssistant::DexLocationToOatFilename(
      dex_location, kRuntimeISA, &oat_location, &error_msg))
      << error_msg;

  // Ensure we can load that file. Just a precondition.
  {
    std::unique_ptr<OatFile> odex_file(OatFile::Open(/*zip_fd=*/-1,
                                                     oat_location,
                                                     oat_location,
                                                     /*executable=*/false,
                                                     /*low_4gb=*/false,
                                                     dex_location,
                                                     &error_msg));
    ASSERT_TRUE(odex_file != nullptr);
    ASSERT_EQ(2u, odex_file->GetOatDexFiles().size());
  }

  // Now replace the source.
  Copy(GetTestDexFileName("MainUncompressedAligned"), dex_location);

  // And try to load again.
  std::unique_ptr<OatFile> odex_file(OatFile::Open(/*zip_fd=*/-1,
                                                   oat_location,
                                                   oat_location,
                                                   /*executable=*/false,
                                                   /*low_4gb=*/false,
                                                   dex_location,
                                                   &error_msg));
  EXPECT_TRUE(odex_file == nullptr);
  EXPECT_NE(std::string::npos, error_msg.find("expected 2 uncompressed dex files, but found 1"))
      << error_msg;
}

TEST_F(OatFileTest, DlOpenLoad) {
  std::string dex_location = GetScratchDir() + "/LoadOat.jar";

  Copy(GetDexSrc1(), dex_location);
  GenerateOatForTest(dex_location.c_str(), CompilerFilter::kSpeed);

  std::string oat_location;
  std::string error_msg;
  ASSERT_TRUE(OatFileAssistant::DexLocationToOatFilename(
      dex_location, kRuntimeISA, &oat_location, &error_msg))
      << error_msg;

  // Clear previous errors if any.
  dlerror();
  error_msg.clear();
  std::unique_ptr<OatFile> odex_file(OatFile::Open(/*zip_fd=*/-1,
                                                   oat_location,
                                                   oat_location,
                                                   /*executable=*/true,
                                                   /*low_4gb=*/false,
                                                   dex_location,
                                                   &error_msg));
  ASSERT_NE(odex_file.get(), nullptr) << error_msg;

#ifdef __GLIBC__
  if (!error_msg.empty()) {
    // If a valid oat file was returned but there was an error message, then dlopen failed
    // but the backup ART ELF loader successfully loaded the oat file.
    // The only expected reason for this is a bug in glibc that prevents loading dynamic
    // shared objects with a read-only dynamic section:
    // https://sourceware.org/bugzilla/show_bug.cgi?id=28340.
    ASSERT_TRUE(error_msg == "DlOpen does not support read-only .dynamic section.") << error_msg;
    GTEST_SKIP() << error_msg;
  }
#else
  // If a valid oat file was returned with no error message, then dlopen was successful.
  ASSERT_TRUE(error_msg.empty()) << error_msg;
#endif

  const char *dlerror_msg = dlerror();
  ASSERT_EQ(dlerror_msg, nullptr) << dlerror_msg;

  // Ensure that the oat file is loaded with dlopen by requesting information about it
  // using dladdr.
  Dl_info info;
  ASSERT_NE(dladdr(odex_file->Begin(), &info), 0);
  EXPECT_STREQ(info.dli_fname, oat_location.c_str())
      << "dli_fname: " << info.dli_fname
      << ", location: " << oat_location;
  EXPECT_STREQ(info.dli_sname, "oatdata") << info.dli_sname;
}

}  // namespace art
