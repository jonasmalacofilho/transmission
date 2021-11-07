/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"

#include "torrent-metainfo-private.h"

#include <gtest/gtest.h>

using namespace std::literals;

TEST(BlockMetainfoTest, DoesNotCrashOnZeroPieceSize)
{
    auto block = tr_block_metainfo(0, 0);
    EXPECT_EQ(0, block.n_blocks);
    EXPECT_EQ(0, block.n_blocks_in_piece);
    EXPECT_EQ(0, block.n_blocks_in_final_piece);
    EXPECT_EQ(0, block.block_size);
    EXPECT_EQ(0, block.final_block_size);
    EXPECT_EQ(0, block.final_piece_size);
}

TEST(BlockMetainfoTest, FinalPieceHasRemainder)
{
    auto const total_size = 2290895707ULL;
    auto const piece_size = 2097152ULL;
    auto const block = tr_block_metainfo(total_size, piece_size);

    EXPECT_EQ(128, block.n_blocks_in_piece);
    EXPECT_EQ(139826, block.n_blocks);
    EXPECT_EQ(16384, block.block_size);
    EXPECT_EQ(2907, block.final_block_size);
    EXPECT_EQ(50, block.n_blocks_in_final_piece);
    EXPECT_EQ(805723, block.final_piece_size);
}

TEST(BlockMetainfoTest, FinalPiecePerfectFit)
{
    auto const total_size = 1048576ULL;
    auto const piece_size = 131072ULL;
    auto const block = tr_block_metainfo(total_size, piece_size);

    EXPECT_EQ(131072ULL, block.final_piece_size);
    EXPECT_EQ(16384, block.block_size);
    EXPECT_EQ(16384, block.final_block_size);
    EXPECT_EQ(64, block.n_blocks);
    EXPECT_EQ(8, block.n_blocks_in_final_piece);
    EXPECT_EQ(8, block.n_blocks_in_piece);
}
