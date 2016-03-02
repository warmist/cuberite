
#include "Globals.h"  // NOTE: MSVC stupidness requires this to be the same across all modules

#include "Enderman.h"
#include "../Entities/Player.h"
#include "../Tracer.h"
#include "Chunk.h"



////////////////////////////////////////////////////////////////////////////////
// cPlayerLookCheck
class cPlayerLookCheck :
	public cPlayerListCallback
{
public:
	cPlayerLookCheck(Vector3d a_EndermanPos, int a_SightDistance) :
		m_Player(nullptr),
		m_EndermanPos(a_EndermanPos),
		m_SightDistance(a_SightDistance)
	{
	}

	virtual bool Item(cPlayer * a_Player) override
	{
		// Don't check players who are in creative gamemode
		if (a_Player->IsGameModeCreative())
		{
			return false;
		}

		Vector3d Direction = m_EndermanPos - a_Player->GetPosition();

		// Don't check players who are more then SightDistance (64) blocks away
		if (Direction.Length() > m_SightDistance)
		{
			return false;
		}

		// Don't check if the player has a pumpkin on his head
		if (a_Player->GetEquippedHelmet().m_ItemType == E_BLOCK_PUMPKIN)
		{
			return false;
		}


		Vector3d LookVector = a_Player->GetLookVector();
		double dot = Direction.Dot(LookVector);

		// 0.09 rad ~ 5 degrees
		// If the player's crosshair is within 5 degrees of the enderman, it counts as looking
		if (dot <= cos(0.09))
		{
			return false;
		}

		cTracer LineOfSight(a_Player->GetWorld());
		if (LineOfSight.Trace(m_EndermanPos, Direction, static_cast<int>(Direction.Length())))
		{
			// No direct line of sight
			return false;
		}

		m_Player = a_Player;
		return true;
	}

	cPlayer * GetPlayer(void) const { return m_Player; }

protected:
	cPlayer * m_Player;
	Vector3d m_EndermanPos;
	int m_SightDistance;
} ;





cEnderman::cEnderman(void) :
	super("Enderman", mtEnderman, "mob.endermen.hit", "mob.endermen.death", 0.5, 2.9),
	m_bIsScreaming(false),
	CarriedBlock(E_BLOCK_AIR),
	CarriedMeta(0)
{
}





void cEnderman::GetDrops(cItems & a_Drops, cEntity * a_Killer)
{
	unsigned int LootingLevel = 0;
	if (a_Killer != nullptr)
	{
		LootingLevel = a_Killer->GetEquippedWeapon().m_Enchantments.GetLevel(cEnchantments::enchLooting);
	}
	AddRandomDropItem(a_Drops, 0, 1 + LootingLevel, E_ITEM_ENDER_PEARL);
}




void cEnderman::CheckEventSeePlayer(cChunk & a_Chunk)
{
	if (GetTarget() != nullptr)
	{
		return;
	}

	cPlayerLookCheck Callback(GetPosition(), m_SightDistance);
	if (m_World->ForEachPlayer(Callback))
	{
		return;
	}

	ASSERT(Callback.GetPlayer() != nullptr);

	if (!CheckLight())
	{
		// Insufficient light for enderman to become aggravated
		// TODO: Teleport to a suitable location
		return;
	}

	if (!Callback.GetPlayer()->IsGameModeCreative())
	{
		cMonster::EventSeePlayer(Callback.GetPlayer(), a_Chunk);
		m_EMState = CHASING;
		m_bIsScreaming = true;
		GetWorld()->BroadcastEntityMetadata(*this);
	}
}





void cEnderman::CheckEventLostPlayer(void)
{
	super::CheckEventLostPlayer();
	if (!CheckLight())
	{
		EventLosePlayer();
	}
}





void cEnderman::EventLosePlayer()
{
	super::EventLosePlayer();
	m_bIsScreaming = false;
	GetWorld()->BroadcastEntityMetadata(*this);
}





bool cEnderman::CheckLight()
{
	int ChunkX, ChunkZ;
	cChunkDef::BlockToChunk(POSX_TOINT, POSZ_TOINT, ChunkX, ChunkZ);

	// Check if the chunk the enderman is in is lit
	if (!m_World->IsChunkLighted(ChunkX, ChunkZ))
	{
		m_World->QueueLightChunk(ChunkX, ChunkZ);
		return true;
	}

	// Enderman only attack if the skylight is lower or equal to 8
	if (m_World->GetBlockSkyLight(POSX_TOINT, POSY_TOINT, POSZ_TOINT) - GetWorld()->GetSkyDarkness() > 8)
	{
		return false;
	}

	return true;
}





void cEnderman::Tick(std::chrono::milliseconds a_Dt, cChunk & a_Chunk)
{
	super::Tick(a_Dt, a_Chunk);

	// TODO take damage in rain

	// Take damage when touching water, drowning damage seems to be most appropriate
	if (IsSwimming())
	{
		EventLosePlayer();
		TakeDamage(dtDrowning, nullptr, 1, 0);
		// TODO teleport to a safe location
	}

}


void cEnderman::InStateIdle(std::chrono::milliseconds a_Dt, cChunk & a_Chunk)
{
    if (m_PathfinderActivated)
    {
        return;  // Still getting there
    }

    if (m_IdleInterval > std::chrono::seconds(1)) //been idle for a sec
    {
        int rem = m_World->GetTickRandomNumber(9);
        if (rem == 9) //10% chance
        {
            BlockPickingLogic(a_Chunk);
        }
    }
    super::InStateIdle(a_Dt, a_Chunk);//do other idle stuff
}

void cEnderman::BlockPickingLogic(cChunk & a_Chunk)
{
    bool placing = (CarriedBlock != E_BLOCK_AIR);

    Vector3d Dist;
    if (placing)
    {
        //Endermen place blocks to 2x2 region (and 2 height) centered around them.
        Dist.x = static_cast<double>(m_World->GetTickRandomNumber(2)) - 1.0;
        Dist.z = static_cast<double>(m_World->GetTickRandomNumber(2)) - 1.0;
        Dist.y = static_cast<double>(m_World->GetTickRandomNumber(2));
    }
    else
    {
        //Endermen take blocks from 4x4 region (and 3 height) centered around them.
        Dist.x = static_cast<double>(m_World->GetTickRandomNumber(4)) - 2.0;
        Dist.z = static_cast<double>(m_World->GetTickRandomNumber(4)) - 2.0;
        Dist.y = static_cast<double>(m_World->GetTickRandomNumber(3));
    }
    Vector3d Destination(GetPosX() + Dist.x, GetPosY() + Dist.y, GetPosZ() + Dist.z);

    cChunk * Chunk = a_Chunk.GetNeighborChunk(static_cast<int>(Destination.x), static_cast<int>(Destination.z));
    if ((Chunk == nullptr) || !Chunk->IsValid())
    {
        return;
    }

    BLOCKTYPE BlockType;
    NIBBLETYPE BlockMeta;
    int RelX = static_cast<int>(Destination.x) - Chunk->GetPosX() * cChunkDef::Width;
    int RelZ = static_cast<int>(Destination.z) - Chunk->GetPosZ() * cChunkDef::Width;
    Chunk->GetBlockTypeMeta(RelX, static_cast<int>(Destination.y), RelZ, BlockType, BlockMeta);
    if (placing)
    {
        BLOCKTYPE FloorType;
        NIBBLETYPE FloorMeta;
        Chunk->GetBlockTypeMeta(RelX, static_cast<int>(Destination.y) - 1, RelZ, FloorType, FloorMeta);
        //CALLBACK HERE
        if (BlockType == E_BLOCK_AIR && FloorType!=E_BLOCK_AIR)
        {
            //TODO: check if he(it?) can place it. E.g. can place flower on this block?
            m_World->SetBlock(Destination.x, Destination.y, Destination.z, CarriedBlock, CarriedMeta);

            SetCarriedBlock(E_BLOCK_AIR);
            SetCarriedMeta(0);
            GetWorld()->BroadcastEntityMetadata(*this); //display that we picked up a block
            //CALLBACK HERE
        }
    }
    else
    {
        //CALLBACK HERE
        //check if block is whitelisted
        if (BlockType != E_BLOCK_FLOWER &&
            BlockType != E_BLOCK_CACTUS &&
            BlockType != E_BLOCK_CLAY &&
            BlockType != E_BLOCK_DIRT &&
            BlockType != E_BLOCK_MELON &&
            BlockType != E_BLOCK_PUMPKIN &&
            BlockType != E_BLOCK_MYCELIUM &&
            BlockType != E_BLOCK_RED_MUSHROOM &&
            BlockType != E_BLOCK_BROWN_MUSHROOM &&
            BlockType != E_BLOCK_SAND &&
            BlockType != E_BLOCK_TNT)
        {
            //CALLBACK HERE
            //pick it up
            SetCarriedBlock(BlockType);
            SetCarriedMeta(BlockMeta);
            GetWorld()->BroadcastEntityMetadata(*this); //display that we picked up a block

            m_World->DigBlock(Destination.x, Destination.y, Destination.z);//NOTE(warmist): Should probably use the chunk function but there is none, and we dont want to dublicate code, do we?
            //CALLBACK HERE
        }
    }
}