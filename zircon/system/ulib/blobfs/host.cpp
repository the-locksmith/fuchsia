// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utility>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <fs-host/common.h>
#include <fs/block-txn.h>
#include <fs/trace.h>

#define ZXDEBUG 0

#include <blobfs/compression/compressor.h>
#include <blobfs/compression/zstd.h>
#include <blobfs/format.h>
#include <blobfs/fsck.h>
#include <blobfs/host.h>

using digest::Digest;
using digest::MerkleTree;

constexpr uint32_t kExtentCount = 5;

namespace blobfs {
namespace {

using HostCompressor = ZSTDCompressor;
const auto HostDecompressor = ZSTDDecompress;
constexpr uint32_t kBlobFlagCompressed = kBlobFlagZSTDCompressed;

zx_status_t readblk_offset(int fd, uint64_t bno, off_t offset, void* data) {
    off_t off = offset + bno * kBlobfsBlockSize;
    if (lseek(fd, off, SEEK_SET) < 0) {
        FS_TRACE_ERROR("blobfs: cannot seek to block %" PRIu64 "\n", bno);
        return ZX_ERR_IO;
    }
    if (read(fd, data, kBlobfsBlockSize) != kBlobfsBlockSize) {
        FS_TRACE_ERROR("blobfs: cannot read block %" PRIu64 "\n", bno);
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t writeblk_offset(int fd, uint64_t bno, off_t offset, const void* data) {
    off_t off = offset + bno * kBlobfsBlockSize;
    if (lseek(fd, off, SEEK_SET) < 0) {
        FS_TRACE_ERROR("blobfs: cannot seek to block %" PRIu64 "\n", bno);
        return ZX_ERR_IO;
    }
    if (write(fd, data, kBlobfsBlockSize) != kBlobfsBlockSize) {
        FS_TRACE_ERROR("blobfs: cannot write block %" PRIu64 "\n", bno);
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

// From a buffer, create a merkle tree.
//
// Given a mapped blob at |blob_data| of length |length|, compute the
// Merkle digest and the output merkle tree as a uint8_t array.
zx_status_t buffer_create_merkle(const FileMapping& mapping, MerkleInfo* out_info) {
    zx_status_t status;
    size_t merkle_size = MerkleTree::GetTreeLength(mapping.length());
    auto merkle_tree = fbl::unique_ptr<uint8_t[]>(new uint8_t[merkle_size]);
    if ((status = MerkleTree::Create(mapping.data(), mapping.length(), merkle_tree.get(),
                                     merkle_size, &out_info->digest)) != ZX_OK) {
        return status;
    }
    out_info->merkle.reset(merkle_tree.release(), merkle_size);
    out_info->length = mapping.length();
    return ZX_OK;
}

zx_status_t buffer_compress(const FileMapping& mapping, MerkleInfo* out_info) {
    size_t max = HostCompressor::BufferMax(mapping.length());
    out_info->compressed_data.reset(new uint8_t[max]);
    out_info->compressed = false;

    if (mapping.length() < kCompressionMinBytesSaved) {
        return ZX_OK;
    }

    zx_status_t status;
    fbl::unique_ptr<HostCompressor> compressor;
    if ((status = HostCompressor::Create(mapping.length(),
                                         out_info->compressed_data.get(),
                                         max, &compressor)) != ZX_OK) {
        FS_TRACE_ERROR("Failed to initialize blobfs compressor: %d\n", status);
        return status;
    }

    if ((status = compressor->Update(mapping.data(), mapping.length())) != ZX_OK) {
        FS_TRACE_ERROR("Failed to update blobfs compressor: %d\n", status);
        return status;
    }

    if ((status = compressor->End()) != ZX_OK) {
        FS_TRACE_ERROR("Failed to complete blobfs compressor: %d\n", status);
        return status;
    }

    if (mapping.length() > compressor->Size() + kCompressionMinBytesSaved) {
        out_info->compressed_length = compressor->Size();
        out_info->compressed = true;
    }

    return ZX_OK;
}

// Given a buffer (and pre-computed merkle tree), add the buffer as a
// blob in Blobfs.
zx_status_t blobfs_add_mapped_blob_with_merkle(Blobfs* bs, FileSizeRecorder* size_recorder, 
                                               const FileMapping& mapping, const MerkleInfo& info) {
    ZX_ASSERT(mapping.length() == info.length);
    const void* data;

    if (info.compressed) {
        data = info.compressed_data.get();
    } else {
        data = mapping.data();
    }

    // After we've pre-calculated all necessary information, actually add the
    // blob to the filesystem itself.
    static std::mutex add_blob_mutex_;
    std::lock_guard<std::mutex> lock(add_blob_mutex_);
    fbl::unique_ptr<InodeBlock> inode_block;
    zx_status_t status;
    if ((status = bs->NewBlob(info.digest, &inode_block)) != ZX_OK) {
        FS_TRACE_ERROR("error: Failed to allocate a new blob\n");
        return status;
    }
    if (inode_block == nullptr) {
        FS_TRACE_ERROR("error: No nodes available on blobfs image\n");
        return ZX_ERR_NO_RESOURCES;
    }

    Inode* inode = inode_block->GetInode();
    inode->blob_size = mapping.length();
    uint64_t block_count = MerkleTreeBlocks(*inode) + info.GetDataBlocks();
    if (block_count > std::numeric_limits<uint32_t>::max()) {
        FS_TRACE_ERROR("error: Block count too large\n");
        return ZX_ERR_NOT_SUPPORTED;
    }
    inode->block_count = static_cast<uint32_t>(block_count);
    inode->header.flags |= kBlobFlagAllocated | (info.compressed ? kBlobFlagCompressed : 0);

    // TODO(smklein): Currently, host-side tools can only generate single-extent
    // blobs. This should be fixed.
    if (inode->block_count > kBlockCountMax) {
        FS_TRACE_ERROR("error: Blobs larger than %lu blocks not yet implemented\n",
                       kBlockCountMax);
        return ZX_ERR_NOT_SUPPORTED;
    }

    size_t start_block = 0;
    if ((status = bs->AllocateBlocks(inode->block_count, &start_block)) != ZX_OK) {
        FS_TRACE_ERROR("error: No blocks available\n");
        return status;
    }

    // TODO(smklein): This is hardcoded alongside the check against "kBlockCountMax" above.
    inode->extents[0].SetStart(start_block);
    inode->extents[0].SetLength(static_cast<BlockCountType>(inode->block_count));
    inode->extent_count = 1;

    if (size_recorder) {
        char digest_buf[65];
        info.digest.ToString(digest_buf, sizeof(digest_buf));

        size_recorder->AppendSizeInformation(digest_buf, kBlobfsBlockSize * inode->block_count);
    }

    if ((status = bs->WriteData(inode, info.merkle.get(), data)) != ZX_OK) {
        return status;
    }

    if ((status = bs->WriteBitmap(inode->block_count, inode->extents[0].Start())) != ZX_OK) {
        return status;
    } else if ((status = bs->WriteNode(std::move(inode_block))) != ZX_OK) {
        return status;
    } else if ((status = bs->WriteInfo()) != ZX_OK) {
        return status;
    }

    return ZX_OK;
}

} // namespace

zx_status_t blobfs_create(fbl::unique_ptr<Blobfs>* out, fbl::unique_fd fd) {
    info_block_t info_block;

    if (readblk(fd.get(), 0, (void*)info_block.block) < 0) {
        return ZX_ERR_IO;
    }
    uint64_t blocks;
    zx_status_t status;
    if ((status = GetBlockCount(fd.get(), &blocks)) != ZX_OK) {
        FS_TRACE_ERROR("blobfs: cannot find end of underlying device\n");
        return status;
    } else if ((status = CheckSuperblock(&info_block.info, blocks)) != ZX_OK) {
        FS_TRACE_ERROR("blobfs: Info check failed\n");
        return status;
    }

    fbl::Array<size_t> extent_lengths(new size_t[kExtentCount], kExtentCount);

    extent_lengths[0] = BlockMapStartBlock(info_block.info) * kBlobfsBlockSize;
    extent_lengths[1] = BlockMapBlocks(info_block.info) * kBlobfsBlockSize;
    extent_lengths[2] = NodeMapBlocks(info_block.info) * kBlobfsBlockSize;
    extent_lengths[3] = JournalBlocks(info_block.info) * kBlobfsBlockSize;
    extent_lengths[4] = DataBlocks(info_block.info) * kBlobfsBlockSize;

    if ((status = Blobfs::Create(std::move(fd), 0, info_block, extent_lengths, out)) != ZX_OK) {
        FS_TRACE_ERROR("blobfs: mount failed; could not create blobfs\n");
        return status;
    }

    return ZX_OK;
}

zx_status_t blobfs_create_sparse(fbl::unique_ptr<Blobfs>* out, fbl::unique_fd fd, off_t start,
                                 off_t end, const fbl::Vector<size_t>& extent_vector) {
    if (start >= end) {
        FS_TRACE_ERROR("blobfs: Insufficient space allocated\n");
        return ZX_ERR_INVALID_ARGS;
    }
    if (extent_vector.size() != kExtentCount) {
        FS_TRACE_ERROR("blobfs: Incorrect number of extents\n");
        return ZX_ERR_INVALID_ARGS;
    }

    info_block_t info_block;

    struct stat s;
    if (fstat(fd.get(), &s) < 0) {
        return ZX_ERR_BAD_STATE;
    }

    if (s.st_size < end) {
        FS_TRACE_ERROR("blobfs: Invalid file size\n");
        return ZX_ERR_BAD_STATE;
    } else if (readblk_offset(fd.get(), 0, start, (void*)info_block.block) < 0) {
        return ZX_ERR_IO;
    }

    zx_status_t status;
    if ((status = CheckSuperblock(&info_block.info, (end - start) / kBlobfsBlockSize)) != ZX_OK) {
        FS_TRACE_ERROR("blobfs: Info check failed\n");
        return status;
    }

    fbl::Array<size_t> extent_lengths(new size_t[kExtentCount], kExtentCount);

    extent_lengths[0] = extent_vector[0];
    extent_lengths[1] = extent_vector[1];
    extent_lengths[2] = extent_vector[2];
    extent_lengths[3] = extent_vector[3];
    extent_lengths[4] = extent_vector[4];

    if ((status = Blobfs::Create(std::move(fd), start, info_block, extent_lengths, out)) != ZX_OK) {
        FS_TRACE_ERROR("blobfs: mount failed; could not create blobfs\n");
        return status;
    }

    return ZX_OK;
}

zx_status_t blobfs_preprocess(int data_fd, bool compress, MerkleInfo* out_info) {
    FileMapping mapping;
    zx_status_t status = mapping.Map(data_fd);
    if (status != ZX_OK) {
        return status;
    }

    if ((status = buffer_create_merkle(mapping, out_info)) != ZX_OK) {
        return status;
    }

    if (compress) {
        status = buffer_compress(mapping, out_info);
    }

    return status;
}

zx_status_t blobfs_add_blob(Blobfs* bs, FileSizeRecorder* size_recorder, int data_fd) {
    FileMapping mapping;
    zx_status_t status = mapping.Map(data_fd);
    if (status != ZX_OK) {
        return status;
    }

    // Calculate the actual Merkle tree.
    MerkleInfo info;
    status = buffer_create_merkle(mapping, &info);
    if (status != ZX_OK) {
        return status;
    }

    return blobfs_add_mapped_blob_with_merkle(bs, size_recorder, mapping, info);
}

zx_status_t blobfs_add_blob_with_merkle(Blobfs* bs, FileSizeRecorder* size_recorder, int data_fd, const MerkleInfo& info) {
    FileMapping mapping;
    zx_status_t status = mapping.Map(data_fd);
    if (status != ZX_OK) {
        return status;
    }

    return blobfs_add_mapped_blob_with_merkle(bs, size_recorder, mapping, info);
}

zx_status_t blobfs_fsck(fbl::unique_fd fd, off_t start, off_t end,
                        const fbl::Vector<size_t>& extent_lengths) {
    fbl::unique_ptr<Blobfs> blob;
    zx_status_t status;
    if ((status = blobfs_create_sparse(&blob, std::move(fd), start, end, extent_lengths)) != ZX_OK) {
        return status;
    } else if ((status = Fsck(std::move(blob))) != ZX_OK) {
        return status;
    }
    return ZX_OK;
}

Blobfs::Blobfs(fbl::unique_fd fd, off_t offset, const info_block_t& info_block,
               const fbl::Array<size_t>& extent_lengths)
    : blockfd_(std::move(fd)),
      dirty_(false), offset_(offset) {
    ZX_ASSERT(extent_lengths.size() == kExtentCount);
    memcpy(&info_block_, info_block.block, kBlobfsBlockSize);
    cache_.bno = 0;

    block_map_start_block_ = extent_lengths[0] / kBlobfsBlockSize;
    block_map_block_count_ = extent_lengths[1] / kBlobfsBlockSize;
    node_map_start_block_ = block_map_start_block_ + block_map_block_count_;
    node_map_block_count_ = extent_lengths[2] / kBlobfsBlockSize;
    journal_start_block_ = node_map_start_block_ + node_map_block_count_;
    journal_block_count_ = extent_lengths[3] / kBlobfsBlockSize;
    data_start_block_ = journal_start_block_ + journal_block_count_;
    data_block_count_ = extent_lengths[4] / kBlobfsBlockSize;
}

zx_status_t Blobfs::Create(fbl::unique_fd blockfd_, off_t offset, const info_block_t& info_block,
                           const fbl::Array<size_t>& extent_lengths,
                           fbl::unique_ptr<Blobfs>* out) {
    zx_status_t status = CheckSuperblock(&info_block.info, TotalBlocks(info_block.info));
    if (status < 0) {
        FS_TRACE_ERROR("blobfs: Check info failure\n");
        return status;
    }

    ZX_ASSERT(extent_lengths.size() == kExtentCount);

    for (unsigned i = 0; i < 3; i++) {
        if (extent_lengths[i] % kBlobfsBlockSize) {
            return ZX_ERR_INVALID_ARGS;
        }
    }

    auto fs = fbl::unique_ptr<Blobfs>(new Blobfs(std::move(blockfd_), offset,
                                                 info_block, extent_lengths));

    if ((status = fs->LoadBitmap()) < 0) {
        FS_TRACE_ERROR("blobfs: Failed to load bitmaps\n");
        return status;
    }

    *out = std::move(fs);
    return ZX_OK;
}

zx_status_t Blobfs::LoadBitmap() {
    zx_status_t status;
    if ((status = block_map_.Reset(block_map_block_count_ * kBlobfsBlockBits)) != ZX_OK) {
        return status;
    } else if ((status = block_map_.Shrink(info_.data_block_count)) != ZX_OK) {
        return status;
    }
    const void* bmstart = block_map_.StorageUnsafe()->GetData();

    for (size_t n = 0; n < block_map_block_count_; n++) {
        void* bmdata = fs::GetBlock(kBlobfsBlockSize, bmstart, n);

        if (n >= node_map_start_block_) {
            memset(bmdata, 0, kBlobfsBlockSize);
        } else if ((status = ReadBlock(block_map_start_block_ + n)) != ZX_OK) {
            return status;
        } else {
            memcpy(bmdata, cache_.blk, kBlobfsBlockSize);
        }
    }
    return ZX_OK;
}

zx_status_t Blobfs::NewBlob(const Digest& digest, fbl::unique_ptr<InodeBlock>* out) {
    size_t ino = info_.inode_count;

    for (size_t i = 0; i < info_.inode_count; ++i) {
        size_t bno = (i / kBlobfsInodesPerBlock) + node_map_start_block_;

        zx_status_t status;
        if ((status = ReadBlock(bno)) != ZX_OK) {
            return status;
        }

        auto iblk = reinterpret_cast<const Inode*>(cache_.blk);
        auto observed_inode = &iblk[i % kBlobfsInodesPerBlock];
        if (observed_inode->header.IsAllocated() && !observed_inode->header.IsExtentContainer()) {
            if (digest == observed_inode->merkle_root_hash) {
                return ZX_ERR_ALREADY_EXISTS;
            }
        } else if (ino >= info_.inode_count) {
            // If |ino| has not already been set to a valid value, set it to the
            // first free value we find.
            // We still check all the remaining inodes to avoid adding a duplicate blob.
            ino = i;
        }
    }

    if (ino >= info_.inode_count) {
        return ZX_ERR_NO_RESOURCES;
    }

    size_t bno = (ino / kBlobfsInodesPerBlock) + NodeMapStartBlock(info_);
    zx_status_t status;
    if ((status = ReadBlock(bno)) != ZX_OK) {
        return status;
    }

    Inode* inodes = reinterpret_cast<Inode*>(cache_.blk);

    fbl::unique_ptr<InodeBlock> ino_block(
        new InodeBlock(bno, &inodes[ino % kBlobfsInodesPerBlock], digest));

    dirty_ = true;
    info_.alloc_inode_count++;
    *out = std::move(ino_block);
    return ZX_OK;
}

zx_status_t Blobfs::AllocateBlocks(size_t nblocks, size_t* blkno_out) {
    zx_status_t status;
    if ((status = block_map_.Find(false, 0, block_map_.size(), nblocks, blkno_out)) != ZX_OK) {
        return status;
    } else if ((status = block_map_.Set(*blkno_out, *blkno_out + nblocks)) != ZX_OK) {
        return status;
    }

    info_.alloc_block_count += nblocks;
    return ZX_OK;
}

zx_status_t Blobfs::WriteBitmap(size_t nblocks, size_t start_block) {
    uint64_t bbm_start_block = start_block / kBlobfsBlockBits;
    uint64_t bbm_end_block = fbl::round_up(start_block + nblocks, kBlobfsBlockBits) / kBlobfsBlockBits;
    const void* bmstart = block_map_.StorageUnsafe()->GetData();
    for (size_t n = bbm_start_block; n < bbm_end_block; n++) {
        const void* data = fs::GetBlock(kBlobfsBlockSize, bmstart, n);
        uint64_t bno = block_map_start_block_ + n;
        zx_status_t status;
        if ((status = WriteBlock(bno, data)) != ZX_OK) {
            return status;
        }
    }

    return ZX_OK;
}

zx_status_t Blobfs::WriteNode(fbl::unique_ptr<InodeBlock> ino_block) {
    if (ino_block->GetBno() != cache_.bno) {
        return ZX_ERR_ACCESS_DENIED;
    }

    dirty_ = false;
    return WriteBlock(cache_.bno, cache_.blk);
}

zx_status_t Blobfs::WriteData(Inode* inode, const void* merkle_data, const void* blob_data) {
    const size_t merkle_blocks = MerkleTreeBlocks(*inode);
    const size_t data_blocks = inode->block_count - merkle_blocks;
    for (size_t n = 0; n < merkle_blocks; n++) {
        const void* data = fs::GetBlock(kBlobfsBlockSize, merkle_data, n);
        uint64_t bno = data_start_block_ + inode->extents[0].Start() + n;
        zx_status_t status;
        if ((status = WriteBlock(bno, data)) != ZX_OK) {
            return status;
        }
    }

    for (size_t n = 0; n < data_blocks; n++) {
        const void* data = fs::GetBlock(kBlobfsBlockSize, blob_data, n);

        // If we try to write a block, will it be reaching beyond the end of the
        // mapped file?
        size_t off = n * kBlobfsBlockSize;
        uint8_t last_data[kBlobfsBlockSize];
        if (inode->blob_size < off + kBlobfsBlockSize) {
            // Read the partial block from a block-sized buffer which zero-pads the data.
            memset(last_data, 0, kBlobfsBlockSize);
            memcpy(last_data, data, inode->blob_size - off);
            data = last_data;
        }

        uint64_t bno = data_start_block_ + inode->extents[0].Start() + merkle_blocks + n;
        zx_status_t status;
        if ((status = WriteBlock(bno, data)) != ZX_OK) {
            return status;
        }
    }

    return ZX_OK;
}

zx_status_t Blobfs::WriteInfo() {
    return WriteBlock(0, info_block_);
}

zx_status_t Blobfs::ReadBlock(size_t bno) {
    if (dirty_) {
        return ZX_ERR_ACCESS_DENIED;
    }

    zx_status_t status;
    if ((cache_.bno != bno) && ((status = readblk_offset(blockfd_.get(), bno, offset_, &cache_.blk)) != ZX_OK)) {
        return status;
    }

    cache_.bno = bno;
    return ZX_OK;
}

zx_status_t Blobfs::WriteBlock(size_t bno, const void* data) {
    return writeblk_offset(blockfd_.get(), bno, offset_, data);
}

zx_status_t Blobfs::ResetCache() {
    if (dirty_) {
        return ZX_ERR_ACCESS_DENIED;
    }

    if (cache_.bno != 0) {
        memset(cache_.blk, 0, kBlobfsBlockSize);
        cache_.bno = 0;
    }
    return ZX_OK;
}

Inode* Blobfs::GetNode(uint32_t index) {
    size_t bno = node_map_start_block_ + index / kBlobfsInodesPerBlock;

    if (bno >= data_start_block_) {
        // Set cache to 0 so we can return a pointer to an empty inode
        if (ResetCache() != ZX_OK) {
            return nullptr;
        }
    } else if (ReadBlock(bno) < 0) {
        return nullptr;
    }

    auto iblock = reinterpret_cast<Inode*>(cache_.blk);
    return &iblock[index % kBlobfsInodesPerBlock];
}

zx_status_t Blobfs::VerifyBlob(uint32_t node_index) {
    Inode inode = *GetNode(node_index);

    // Determine size for (uncompressed) data buffer.
    uint64_t data_blocks = BlobDataBlocks(inode);
    uint64_t merkle_blocks = MerkleTreeBlocks(inode);
    uint64_t num_blocks = data_blocks + merkle_blocks;
    size_t target_size;
    if (mul_overflow(num_blocks, kBlobfsBlockSize, &target_size)) {
        FS_TRACE_ERROR("Multiplication overflow");
        return ZX_ERR_OUT_OF_RANGE;
    }

    // Create data buffer.
    fbl::unique_ptr<uint8_t[]> data(new uint8_t[target_size]);
    if (inode.header.flags & kBlobFlagCompressed) {
        // Read in uncompressed merkle blocks.
        for (unsigned i = 0; i < merkle_blocks; i++) {
            ReadBlock(data_start_block_ + inode.extents[0].Start() + i);
            memcpy(data.get() + (i * kBlobfsBlockSize), cache_.blk, kBlobfsBlockSize);
        }

        // Determine size for compressed data buffer.
        size_t compressed_blocks = (inode.block_count - merkle_blocks);
        size_t compressed_size;
        if (mul_overflow(compressed_blocks, kBlobfsBlockSize, &compressed_size)) {
            FS_TRACE_ERROR("Multiplication overflow");
            return ZX_ERR_OUT_OF_RANGE;
        }

        // Create compressed data buffer.
        fbl::unique_ptr<uint8_t[]> compressed_data(new uint8_t[compressed_size]);

        // Read in all compressed blob data.
        for (unsigned i = 0; i < compressed_blocks; i++) {
            ReadBlock(data_start_block_ + inode.extents[0].Start() + i + merkle_blocks);
            memcpy(compressed_data.get() + (i * kBlobfsBlockSize), cache_.blk, kBlobfsBlockSize);
        }

        // Decompress the compressed data into the target buffer.
        zx_status_t status;
        target_size = inode.blob_size;
        uint8_t* data_ptr = data.get() + (merkle_blocks * kBlobfsBlockSize);
        if ((status = HostDecompressor(data_ptr, &target_size, compressed_data.get(),
                                       &compressed_size)) != ZX_OK) {
            return status;
        }
        if (target_size != inode.blob_size) {
            FS_TRACE_ERROR("Failed to fully decompress blob (%zu of %" PRIu64 " expected)\n",
                           target_size, inode.blob_size);
            return ZX_ERR_IO_DATA_INTEGRITY;
        }
    } else {
        // For uncompressed blobs, read entire blob straight into the data buffer.
        for (unsigned i = 0; i < inode.block_count; i++) {
            ReadBlock(data_start_block_ + inode.extents[0].Start() + i);
            memcpy(data.get() + (i * kBlobfsBlockSize), cache_.blk, kBlobfsBlockSize);
        }
    }

    // Verify the contents of the blob.
    uint8_t* data_ptr = data.get() + (merkle_blocks * kBlobfsBlockSize);
    Digest digest(&inode.merkle_root_hash[0]);
    return MerkleTree::Verify(data_ptr, inode.blob_size, data.get(),
                              MerkleTree::GetTreeLength(inode.blob_size), 0,
                              inode.blob_size, digest);
}
} // namespace blobfs

// This is used by the ioctl wrappers in magenta/device/device.h. It's not
// called by host tools, so just satisfy the linker with a stub.
ssize_t fdio_ioctl(int fd, int op, const void* in_buf, size_t in_len, void* out_buf,
                   size_t out_len) {
    return -1;
}
