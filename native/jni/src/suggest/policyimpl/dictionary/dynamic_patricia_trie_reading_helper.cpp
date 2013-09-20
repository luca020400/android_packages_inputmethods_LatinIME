/*
 * Copyright (C) 2013, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "suggest/policyimpl/dictionary/dynamic_patricia_trie_reading_helper.h"

#include "suggest/policyimpl/dictionary/utils/buffer_with_extendable_buffer.h"

namespace latinime {

// To avoid infinite loop caused by invalid or malicious forward links.
const int DynamicPatriciaTrieReadingHelper::MAX_CHILD_COUNT_TO_AVOID_INFINITE_LOOP = 100000;
const int DynamicPatriciaTrieReadingHelper::MAX_NODE_ARRAY_COUNT_TO_AVOID_INFINITE_LOOP = 100000;
const size_t DynamicPatriciaTrieReadingHelper::MAX_READING_STATE_STACK_SIZE = MAX_WORD_LENGTH;

bool DynamicPatriciaTrieReadingHelper::traverseAllPtNodesInPostorderDepthFirstManner(
        TraversingEventListener *const listener) {
    bool alreadyVisitedChildren = false;
    // Descend from the root to the root PtNode array.
    if (!listener->onDescend()) {
        return false;
    }
    while (!isEnd()) {
        if (!alreadyVisitedChildren) {
            if (mNodeReader.hasChildren()) {
                // Move to the first child.
                if (!listener->onDescend()) {
                    return false;
                }
                pushReadingStateToStack();
                readChildNode();
            } else {
                alreadyVisitedChildren = true;
            }
        } else {
            if (!listener->onVisitingPtNode(&mNodeReader)) {
                return false;
            }
            readNextSiblingNode();
            if (isEnd()) {
                // All PtNodes in current linked PtNode arrays have been visited.
                // Return to the parent.
                if (!listener->onAscend()) {
                    return false;
                }
                popReadingStateFromStack();
                alreadyVisitedChildren = true;
            } else {
                // Process sibling PtNode.
                alreadyVisitedChildren = false;
            }
        }
    }
    // Ascend from the root PtNode array to the root.
    if (!listener->onAscend()) {
        return false;
    }
    return !isError();
}

// Read node array size and process empty node arrays. Nodes and arrays are counted up in this
// method to avoid an infinite loop.
void DynamicPatriciaTrieReadingHelper::nextNodeArray() {
    mReadingState.mPosOfLastPtNodeArrayHead = mReadingState.mPos;
    const bool usesAdditionalBuffer = mBuffer->isInAdditionalBuffer(mReadingState.mPos);
    const uint8_t *const dictBuf = mBuffer->getBuffer(usesAdditionalBuffer);
    if (usesAdditionalBuffer) {
        mReadingState.mPos -= mBuffer->getOriginalBufferSize();
    }
    mReadingState.mNodeCount = PatriciaTrieReadingUtils::getPtNodeArraySizeAndAdvancePosition(
            dictBuf, &mReadingState.mPos);
    if (usesAdditionalBuffer) {
        mReadingState.mPos += mBuffer->getOriginalBufferSize();
    }
    // Count up nodes and node arrays to avoid infinite loop.
    mReadingState.mTotalNodeCount += mReadingState.mNodeCount;
    mReadingState.mNodeArrayCount++;
    if (mReadingState.mNodeCount < 0
            || mReadingState.mTotalNodeCount > MAX_CHILD_COUNT_TO_AVOID_INFINITE_LOOP
            || mReadingState.mNodeArrayCount > MAX_NODE_ARRAY_COUNT_TO_AVOID_INFINITE_LOOP) {
        // Invalid dictionary.
        AKLOGI("Invalid dictionary. nodeCount: %d, totalNodeCount: %d, MAX_CHILD_COUNT: %d"
                "nodeArrayCount: %d, MAX_NODE_ARRAY_COUNT: %d",
                mReadingState.mNodeCount, mReadingState.mTotalNodeCount,
                MAX_CHILD_COUNT_TO_AVOID_INFINITE_LOOP, mReadingState.mNodeArrayCount,
                MAX_NODE_ARRAY_COUNT_TO_AVOID_INFINITE_LOOP);
        ASSERT(false);
        mIsError = true;
        mReadingState.mPos = NOT_A_DICT_POS;
        return;
    }
    if (mReadingState.mNodeCount == 0) {
        // Empty node array. Try following forward link.
        followForwardLink();
    }
}

// Follow the forward link and read the next node array if exists.
void DynamicPatriciaTrieReadingHelper::followForwardLink() {
    const bool usesAdditionalBuffer = mBuffer->isInAdditionalBuffer(mReadingState.mPos);
    const uint8_t *const dictBuf = mBuffer->getBuffer(usesAdditionalBuffer);
    if (usesAdditionalBuffer) {
        mReadingState.mPos -= mBuffer->getOriginalBufferSize();
    }
    const int forwardLinkPosition =
            DynamicPatriciaTrieReadingUtils::getForwardLinkPosition(dictBuf, mReadingState.mPos);
    if (usesAdditionalBuffer) {
        mReadingState.mPos += mBuffer->getOriginalBufferSize();
    }
    mReadingState.mPosOfLastForwardLinkField = mReadingState.mPos;
    if (DynamicPatriciaTrieReadingUtils::isValidForwardLinkPosition(forwardLinkPosition)) {
        // Follow the forward link.
        mReadingState.mPos += forwardLinkPosition;
        nextNodeArray();
    } else {
        // All node arrays have been read.
        mReadingState.mPos = NOT_A_DICT_POS;
    }
}

} // namespace latinime
