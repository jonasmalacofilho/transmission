/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"

#include "block-info.h"

#include <gtest/gtest.h>

using namespace std::literals;

TEST(BlockInfoTest, DoesNotCrashOnZeroPieceSize)
{
    auto block = tr_block_info{};
    block.initBlockInfo(0, 0);
    EXPECT_EQ(0, block.n_blocks);
    EXPECT_EQ(0, block.n_blocks_in_piece);
    EXPECT_EQ(0, block.n_blocks_in_final_piece);
    EXPECT_EQ(0, block.block_size);
    EXPECT_EQ(0, block.final_block_size);
    EXPECT_EQ(0, block.final_piece_size);
}

TEST(BlockInfoTest, FinalPieceHasRemainder)
{
    auto const total_size = 2290895707ULL;
    auto const piece_size = 2097152ULL;

    auto block = tr_block_info{};
    block.initBlockInfo(total_size, piece_size);

    EXPECT_EQ(128, block.n_blocks_in_piece);
    EXPECT_EQ(139826, block.n_blocks);
    EXPECT_EQ(16384, block.block_size);
    EXPECT_EQ(2907, block.final_block_size);
    EXPECT_EQ(50, block.n_blocks_in_final_piece);
    EXPECT_EQ(805723, block.final_piece_size);
}

TEST(BlockInfoTest, FinalPiecePerfectFit)
{
    auto const total_size = 1048576ULL;
    auto const piece_size = 131072ULL;

    auto block = tr_block_info{};
    block.initBlockInfo(total_size, piece_size);

    EXPECT_EQ(131072ULL, block.final_piece_size);
    EXPECT_EQ(16384, block.block_size);
    EXPECT_EQ(16384, block.final_block_size);
    EXPECT_EQ(64, block.n_blocks);
    EXPECT_EQ(8, block.n_blocks_in_final_piece);
    EXPECT_EQ(8, block.n_blocks_in_piece);
}
