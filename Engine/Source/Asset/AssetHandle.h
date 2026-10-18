#pragma once

#include "Asset/AssetId.h"
#include "Asset/AssetTracker.h"

namespace Canape
{
	class AssetManager;

	// Move-only reference to an asset owned by AssetManager. Holds no asset pointer;
	// resolve through AssetManager (for example GetMesh) on every access.
	// Destruction releases one reference and deletes this handle's tracker record.
	// Member definitions that call AssetManager live at the end of AssetManager.h.
	template <typename T>
	class AssetHandle
	{
	public:
		AssetHandle() = default;
		~AssetHandle() { Reset(); }

		AssetHandle(const AssetHandle&) = delete;
		AssetHandle& operator=(const AssetHandle&) = delete;

		AssetHandle(AssetHandle&& other) noexcept
			: m_Id(other.m_Id), m_RecordId(other.m_RecordId), m_Manager(other.m_Manager)
		{
			other.Clear();
		}

		AssetHandle& operator=(AssetHandle&& other) noexcept
		{
			if (this != &other)
			{
				Reset();
				m_Id = other.m_Id;
				m_RecordId = other.m_RecordId;
				m_Manager = other.m_Manager;
				other.Clear();
			}
			return *this;
		}

		bool IsValid() const { return m_Manager && m_Id.IsValid(); }
		AssetId<T> GetId() const { return m_Id; }

		// Releases the reference early. The handle becomes invalid.
		void Reset();

	private:
		friend class AssetManager;

		AssetHandle(AssetId<T> id, AssetRecordId recordId, AssetManager* manager)
			: m_Id(id), m_RecordId(recordId), m_Manager(manager)
		{
		}

		void Clear()
		{
			m_Id = {};
			m_RecordId = {};
			m_Manager = nullptr;
		}

		AssetId<T> m_Id{};
		AssetRecordId m_RecordId{};
		AssetManager* m_Manager = nullptr;
	};

	using MeshAssetHandle = AssetHandle<MeshAsset>;
	using TextureAssetHandle = AssetHandle<TextureAsset>;
	using MaterialAssetHandle = AssetHandle<MaterialAsset>;
}
