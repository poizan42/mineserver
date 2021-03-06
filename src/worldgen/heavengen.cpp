/*
Copyright (c) 2010, The Mineserver Project
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
* Neither the name of the The Mineserver Project nor the
names of its contributors may be used to endorse or promote products
derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
#include <cmath>
#include <ctime>
#include <stdio.h>
#include <stdlib.h>


// libnoise
#ifdef LIBNOISE
#include <libnoise/noise.h>
#else
#include <noise/noise.h>
#endif

#include "heavengen.h"
#include "cavegen.h"

#include "mineserver.h"
#include "config.h"
#include "constants.h"
#include "logger.h"
#include "map.h"
#include "nbt.h"
#include "tree.h"

#include "tools.h"
#include "random.h"

int heaven_seed;

HeavenGen::HeavenGen()
  : heightmap(16 * 16, 0)
{
}


static inline int fastrand()
{
  heaven_seed = (214013 * heaven_seed + 2531011);
  return (heaven_seed >> 16) & 0x7FFF;
}

void HeavenGen::init(int seed)
{
  heaven_seed = seed;

  Randomgen.SetSeed(seed);
  Randomgen.SetOctaveCount(6);
  Randomgen.SetFrequency(1.0 / 180.0);
  Randomgen.SetLacunarity(2.0);
  Randomgen2.SetSeed(seed);
  Randomgen2.SetOctaveCount(6);
  Randomgen2.SetFrequency(1.0 / 180.0);
  Randomgen2.SetLacunarity(2.0);

  seaLevel = ServerInstance->config()->iData("mapgen.sea.level");
  addTrees = false;//ServerInstance->config()->bData("mapgen.trees.enabled");
  expandBeaches = false;//ServerInstance->config()->bData("mapgen.beaches.expand");
  beachExtent = false;//ServerInstance->config()->iData("mapgen.beaches.extent");
  beachHeight = false;//ServerInstance->config()->iData("mapgen.beaches.height");
  addOre = true;//ServerInstance->config()->bData("mapgen.caves.ore");
}

void HeavenGen::re_init(int seed)
{
  Randomgen.SetSeed(seed);
}

void HeavenGen::generateChunk(int x, int z, int map)
{
  NBT_Value* main = new NBT_Value(NBT_Value::TAG_COMPOUND);
  NBT_Value* val = new NBT_Value(NBT_Value::TAG_COMPOUND);

  val->Insert("Sections", new NBT_Value(NBT_Value::TAG_LIST, NBT_Value::TAG_COMPOUND));

  val->Insert("HeightMap", new NBT_Value(heightmap));
  val->Insert("Entities", new NBT_Value(NBT_Value::TAG_LIST, NBT_Value::TAG_COMPOUND));
  val->Insert("TileEntities", new NBT_Value(NBT_Value::TAG_LIST, NBT_Value::TAG_COMPOUND));
  val->Insert("LastUpdate", new NBT_Value((int64_t)time(NULL)));
  val->Insert("xPos", new NBT_Value(x));
  val->Insert("zPos", new NBT_Value(z));
  val->Insert("TerrainPopulated", new NBT_Value((char)1));

  main->Insert("Level", val);

  /*  uint32_t chunkid;
  ServerInstance->map()->posToId(x, z, &chunkid);

  ServerInstance->map()->maps[chunkid].x = x;
  ServerInstance->map()->maps[chunkid].z = z; */

  sChunk* chunk = new sChunk();
  chunk->blocks = new uint8_t[16 * 16 * 256];
  chunk->addblocks = new uint8_t[16 * 16 * 256 / 2];
  chunk->data = new uint8_t[16 * 16 * 256 / 2];
  chunk->blocklight = new uint8_t[16 * 16 * 256 / 2];
  chunk->skylight = new uint8_t[16 * 16 * 256 / 2];
  chunk->heightmap = &((*(*val)["HeightMap"]->GetIntArray())[0]);
  chunk->nbt = main;
  chunk->x = x;
  chunk->z = z;

  ServerInstance->map(map)->chunks.insert(ChunkMap::value_type(ChunkMap::key_type(x, z), chunk));

  memset(chunk->blocks, 0, 16*16*256);
  memset(chunk->addblocks, 0, 16*16*256/2);
  memset(chunk->data, 0, 16*16*256/2);
  memset(chunk->blocklight, 0, 16*16*256/2);
  memset(chunk->skylight, 0, 16*16*256/2);
  chunk->chunks_present = 0xffff;

  generateWithNoise(x, z, map);

  // Update last used time
  //ServerInstance->map()->mapLastused[chunkid] = (int)time(0);

  // Not changed
  //ServerInstance->map()->mapChanged[chunkid] = ServerInstance->config()->bData("save_unchanged_chunks");

  //ServerInstance->map()->maps[chunkid].nbt = main;

  if (addOre)
  {
    AddOre(x, z, map, BLOCK_STATIONARY_WATER);
  }

  // Add trees
  if (addTrees)
  {
    AddTrees(x, z, map,  fastrand() % 2 + 3);
  }

  if (expandBeaches)
  {
    ExpandBeaches(x, z, map);
  }

}

//#define PRINT_MAPGEN_TIME


void HeavenGen::AddTrees(int x, int z, int map, uint16_t count)
{
  int xBlockpos = x << 4;
  int zBlockpos = z << 4;

  int blockX, blockY, blockZ;
  uint8_t block;
  uint8_t meta;

  for (uint16_t i = 0; i < count; i++)
  {
    blockX = fastrand() % 16;
    blockZ = fastrand() % 16;

    blockY = heightmap[(blockZ << 4) + blockX] + 1;

    blockX += xBlockpos;
    blockZ += zBlockpos;

    ServerInstance->map(map)->getBlock(blockX, blockY, blockZ, &block, &meta);
    // No trees on water
    if (block == BLOCK_WATER || block == BLOCK_STATIONARY_WATER)
    {
      continue;
    }

    Tree tree(blockX, blockY, blockZ, map);
  }
}

void HeavenGen::generateWithNoise(int x, int z, int map)
{
  // Debug..
#ifdef PRINT_MAPGEN_TIME
#ifdef WIN32
  DWORD t_begin, t_end;
  t_begin = timeGetTime();
#else
  struct timeval start, end;
  gettimeofday(&start, NULL);
#endif
#endif
  sChunk* chunk = ServerInstance->map(map)->getChunk(x, z);

  // Populate blocks in chunk
  int32_t currentHeight = 0;
  int32_t ymax = 0;
  uint16_t ciel = 0;
  uint8_t* curBlock;
  uint8_t* curData;
  uint8_t col[2] = {0, 8};

  double xBlockpos = x << 4;
  double zBlockpos = z << 4;
  for (int bX = 0; bX < 16; bX++)
  {
    for (int bZ = 0; bZ < 16; bZ++)
    {
      double h = (int8_t)((Randomgen.GetValue(xBlockpos + bX, 0 , zBlockpos + bZ) * 20));
      double n = (int8_t)((Randomgen2.GetValue(xBlockpos + bX, 0, zBlockpos + bZ) * 10) + 64);

      heightmap[(bZ << 4) + bX] = (uint8_t)(h + n);

      for (int bY = 0; bY < 128; bY++)
      {
        int index = bX + (bZ << 4) + (bY << 8);
        curBlock = &(chunk->blocks[index]);
        curData = &(chunk->data[index >> 1]);
        if (bY > n - h && bY < n)
        {
          *curBlock = BLOCK_WOOL;
          *curData = (index & 1) ? col[rand() % 2] : col[rand() % 2] << 4;
          continue;
        }
        *curBlock = BLOCK_AIR;
      }
    }
  }

#ifdef PRINT_MAPGEN_TIME
#ifdef WIN32
  t_end = timeGetTime();
  ServerInstance->logger()->log("Mapgen: " + dtos(t_end - t_begin) + "ms");
#else
  gettimeofday(&end, NULL);
  ServerInstance->logger()->log("Mapgen: " + dtos(end.tv_usec - start.tv_usec));
#endif
#endif
}

void HeavenGen::ExpandBeaches(int x, int z, int map)
{
  int beachExtentSqr = (beachExtent + 1) * (beachExtent + 1);
  int xBlockpos = x << 4;
  int zBlockpos = z << 4;

  int blockX, blockZ, h;
  uint8_t block;
  uint8_t meta;

  for (int bX = 0; bX < 16; bX++)
  {
    for (int bZ = 0; bZ < 16; bZ++)
    {
      blockX = xBlockpos + bX;
      blockZ = zBlockpos + bZ;

      h = heightmap[(bZ << 4) + bX];

      if (h < 0)
      {
        continue;
      }

      bool found = false;
      for (int dx = -beachExtent; !found && dx <= beachExtent; dx++)
      {
        for (int dz = -beachExtent; !found && dz <= beachExtent; dz++)
        {
          for (int dh = -beachHeight; !found && dh <= 0; dh++)
          {
            if (dx * dx + dz * dz + dh * dh > beachExtentSqr)
            {
              continue;
            }

            int xx = bX + dx;
            int zz = bZ + dz;
            int hh = h + dh;
            if (xx < 0 || xx >= 15 || zz < 0 || zz >= 15 || hh < 0 || hh >= 127)
            {
              continue;
            }

            ServerInstance->map(map)->getBlock(xBlockpos + xx, hh, zBlockpos + zz, &block, &meta);
            if (block == BLOCK_WATER || block == BLOCK_STATIONARY_WATER)
            {
              found = true;
              break;
            }
          }
        }
      }
      if (found)
      {
        ServerInstance->map(map)->sendBlockChange(blockX, h, blockZ, BLOCK_SAND, 0);
        ServerInstance->map(map)->setBlock(blockX, h, blockZ, BLOCK_SAND, 0);

        ServerInstance->map(map)->getBlock(blockX, h - 1, blockZ, &block, &meta);

        if (h > 0 && block == BLOCK_DIRT)
        {
          ServerInstance->map(map)->sendBlockChange(blockX, h - 1, blockZ, BLOCK_SAND, 0);
          ServerInstance->map(map)->setBlock(blockX, h - 1, blockZ, BLOCK_SAND, 0);
        }
      }
    }
  }
}

void HeavenGen::AddOre(int x, int z, int map, uint8_t type)
{
  int xBlockpos = x << 4;
  int zBlockpos = z << 4;

  int blockX, blockY, blockZ;
  uint8_t block;
  uint8_t meta;

  int count, startHeight;

  switch (type)
  {
  case BLOCK_STATIONARY_WATER:
    count = fastrand() % 20 + 20;
    startHeight = 128;
    break;
  }

  int i = 0;
  while (i < count)
  {
    blockX = fastrand() % 8 + 4;
    blockZ = fastrand() % 8 + 4;

    blockY = heightmap[(blockZ << 4) + blockX];
    blockY -= 5;

    // Check that startheight is not higher than height at that column
    if (blockY > startHeight)
    {
      blockY = startHeight;
    }

    blockX += xBlockpos;
    blockZ += zBlockpos;

    // Calculate Y
    blockY = fastrand() % blockY;

    i++;

    ServerInstance->map(map)->getBlock(blockX, blockY, blockZ, &block, &meta);
    // No ore in caves
    if (block != BLOCK_WOOL)
    {
      continue;
    }

    AddDeposit(blockX, blockY, blockZ, map, type, 2);

  }
}

void HeavenGen::AddDeposit(int x, int y, int z, int map, uint8_t block, int depotSize)
{
  for (int bX = x; bX < x + depotSize; bX++)
  {
    for (int bY = y; bY < y + depotSize; bY++)
    {
      for (int bZ = z; bZ < z + depotSize; bZ++)
      {
        if (uniform01() < 0.5)
        {
          ServerInstance->map(map)->sendBlockChange(bX, bY, bZ, block, 0);
          ServerInstance->map(map)->setBlock(bX, bY, bZ, block, 0);
        }
      }
    }
  }
}
