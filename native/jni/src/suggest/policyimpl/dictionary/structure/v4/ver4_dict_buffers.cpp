/*
 * Copyright (C) 2013 The Android Open Source Project
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

#include "suggest/policyimpl/dictionary/structure/v4/ver4_dict_buffers.h"

#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>

#include "suggest/policyimpl/dictionary/utils/dict_file_writing_utils.h"
#include "suggest/policyimpl/dictionary/utils/file_utils.h"

namespace latinime {

/* static */ Ver4DictBuffers::Ver4DictBuffersPtr Ver4DictBuffers::openVer4DictBuffers(
        const char *const dictPath, MmappedBuffer::MmappedBufferPtr headerBuffer,
        const FormatUtils::FORMAT_VERSION formatVersion) {
    if (!headerBuffer) {
        ASSERT(false);
        AKLOGE("The header buffer must be valid to open ver4 dict buffers.");
        return Ver4DictBuffersPtr(nullptr);
    }
    // TODO: take only dictDirPath, and open both header and trie files in the constructor below
    const bool isUpdatable = headerBuffer->isUpdatable();
    return Ver4DictBuffersPtr(new Ver4DictBuffers(dictPath, std::move(headerBuffer), isUpdatable,
            formatVersion));
}

bool Ver4DictBuffers::flushHeaderAndDictBuffers(const char *const dictDirPath,
        const BufferWithExtendableBuffer *const headerBuffer) const {
    // Create temporary directory.
    const int tmpDirPathBufSize = FileUtils::getFilePathWithSuffixBufSize(dictDirPath,
            DictFileWritingUtils::TEMP_FILE_SUFFIX_FOR_WRITING_DICT_FILE);
    char tmpDirPath[tmpDirPathBufSize];
    FileUtils::getFilePathWithSuffix(dictDirPath,
            DictFileWritingUtils::TEMP_FILE_SUFFIX_FOR_WRITING_DICT_FILE, tmpDirPathBufSize,
            tmpDirPath);
    if (FileUtils::existsDir(tmpDirPath)) {
        if (!FileUtils::removeDirAndFiles(tmpDirPath)) {
            AKLOGE("Existing directory %s cannot be removed.", tmpDirPath);
            ASSERT(false);
            return false;
        }
    }
    umask(S_IWGRP | S_IWOTH);
    if (mkdir(tmpDirPath, S_IRWXU) == -1) {
        AKLOGE("Cannot create directory: %s. errno: %d.", tmpDirPath, errno);
        return false;
    }
    // Get dictionary base path.
    const int dictNameBufSize = strlen(dictDirPath) + 1 /* terminator */;
    char dictName[dictNameBufSize];
    FileUtils::getBasename(dictDirPath, dictNameBufSize, dictName);
    const int dictPathBufSize = FileUtils::getFilePathBufSize(tmpDirPath, dictName);
    char dictPath[dictPathBufSize];
    FileUtils::getFilePath(tmpDirPath, dictName, dictPathBufSize, dictPath);

    // Write header file.
    if (!DictFileWritingUtils::flushBufferToFileWithSuffix(dictPath,
            Ver4DictConstants::HEADER_FILE_EXTENSION, headerBuffer)) {
        AKLOGE("Dictionary header file %s%s cannot be written.", tmpDirPath,
                Ver4DictConstants::HEADER_FILE_EXTENSION);
        return false;
    }
    // Write trie file.
    if (!DictFileWritingUtils::flushBufferToFileWithSuffix(dictPath,
            Ver4DictConstants::TRIE_FILE_EXTENSION, &mExpandableTrieBuffer)) {
        AKLOGE("Dictionary trie file %s%s cannot be written.", tmpDirPath,
                Ver4DictConstants::TRIE_FILE_EXTENSION);
        return false;
    }
    // Write dictionary contents.
    if (!mTerminalPositionLookupTable.flushToFile(dictPath)) {
        AKLOGE("Terminal position lookup table cannot be written. %s", tmpDirPath);
        return false;
    }
    if (!mProbabilityDictContent.flushToFile(dictPath)) {
        AKLOGE("Probability dict content cannot be written. %s", tmpDirPath);
        return false;
    }
    if (!mBigramDictContent.flushToFile(dictPath)) {
        AKLOGE("Bigram dict content cannot be written. %s", tmpDirPath);
        return false;
    }
    if (!mShortcutDictContent.flushToFile(dictPath)) {
        AKLOGE("Shortcut dict content cannot be written. %s", tmpDirPath);
        return false;
    }
    // Remove existing dictionary.
    if (!FileUtils::removeDirAndFiles(dictDirPath)) {
        AKLOGE("Existing directory %s cannot be removed.", dictDirPath);
        ASSERT(false);
        return false;
    }
    // Rename temporary directory.
    if (rename(tmpDirPath, dictDirPath) != 0) {
        AKLOGE("%s cannot be renamed to %s", tmpDirPath, dictDirPath);
        ASSERT(false);
        return false;
    }
    return true;
}

Ver4DictBuffers::Ver4DictBuffers(const char *const dictPath,
        MmappedBuffer::MmappedBufferPtr headerBuffer, const bool isUpdatable,
        const FormatUtils::FORMAT_VERSION formatVersion)
        : mHeaderBuffer(std::move(headerBuffer)),
          mDictBuffer(MmappedBuffer::openBuffer(dictPath,
                  Ver4DictConstants::TRIE_FILE_EXTENSION, isUpdatable)),
          mHeaderPolicy(mHeaderBuffer->getBuffer(), formatVersion),
          mExpandableHeaderBuffer(mHeaderBuffer ? mHeaderBuffer->getBuffer() : nullptr,
                  mHeaderPolicy.getSize(),
                  BufferWithExtendableBuffer::DEFAULT_MAX_ADDITIONAL_BUFFER_SIZE),
          mExpandableTrieBuffer(mDictBuffer ? mDictBuffer->getBuffer() : nullptr,
                  mDictBuffer ? mDictBuffer->getBufferSize() : 0,
                  BufferWithExtendableBuffer::DEFAULT_MAX_ADDITIONAL_BUFFER_SIZE),
          mTerminalPositionLookupTable(dictPath, isUpdatable),
          mProbabilityDictContent(dictPath, mHeaderPolicy.hasHistoricalInfoOfWords(), isUpdatable),
          mBigramDictContent(dictPath, mHeaderPolicy.hasHistoricalInfoOfWords(), isUpdatable),
          mShortcutDictContent(dictPath, isUpdatable),
          mIsUpdatable(isUpdatable) {}

Ver4DictBuffers::Ver4DictBuffers(const HeaderPolicy *const headerPolicy, const int maxTrieSize)
        : mHeaderBuffer(nullptr), mDictBuffer(nullptr), mHeaderPolicy(headerPolicy),
          mExpandableHeaderBuffer(Ver4DictConstants::MAX_DICTIONARY_SIZE),
          mExpandableTrieBuffer(maxTrieSize), mTerminalPositionLookupTable(),
          mProbabilityDictContent(headerPolicy->hasHistoricalInfoOfWords()),
          mBigramDictContent(headerPolicy->hasHistoricalInfoOfWords()), mShortcutDictContent(),
          mIsUpdatable(true) {}

} // namespace latinime
