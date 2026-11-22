#pragma once

#include "Scene/Component/RenderableComponent.h"
#include "Asset/AssetManager.h"
#include "DX11Renderer/Data/OutlineStyle.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Canape
{
	class MeshRendererComponent : public RenderableComponent
	{
		RTTR_ENABLE(RenderableComponent)

	public:
		MeshRendererComponent() = default;
		void AddRenderItem(const AssetManager& assets, Renderer& renderer) const override;
		bool SetMesh(const std::string& path);
		const MeshAssetHandle& GetMesh() const { return m_Mesh; }
		bool IsOutlineEnabled() const { return m_OutlineEnabled; }
		void SetOutlineEnabled(bool enabled) { m_OutlineEnabled = enabled; }
		const Vector3& GetOutlineColor() const { return m_OutlineStyle.Color; }
		void SetOutlineColor(const Vector3& color);
		uint32_t GetOutlineThickness() const { return m_OutlineStyle.Thickness; }
		void SetOutlineThickness(uint32_t pixels);
		float GetOutlineOpacity() const { return m_OutlineStyle.Opacity; }
		void SetOutlineOpacity(float opacity);
		const OutlineStyle& GetOutlineStyle() const { return m_OutlineStyle; }

		// 슬롯 머티리얼 오버라이드. 빈 이름이면 메쉬 기본 머티리얼을 사용한다.
		bool SetMaterial(uint32_t slot, const std::string& materialName);

		// 실제로 그릴 때 쓰는 머티리얼: 오버라이드가 있으면 오버라이드, 없으면 메쉬 기본 머티리얼.
		const MaterialAssetHandle& GetMaterial(uint32_t slot) const;

		const std::string& GetMeshPath() const { return m_MeshPath; }
		void SetMeshPath(const std::string& path);

		// 직렬화 대상은 오버라이드 이름만. 기본 머티리얼은 메쉬 에셋에서 매번 다시 얻는다.
		const std::vector<std::string>& GetMaterialNames() const { return m_MaterialNames; }
		void SetMaterialNames(const std::vector<std::string>& names);

	private:
		void LoadDefaultMaterials();

		MeshAssetHandle m_Mesh;
		bool m_OutlineEnabled = false;
		OutlineStyle m_OutlineStyle{};
		std::vector<MaterialAssetHandle> m_DefaultMaterials; // 메쉬 머티리얼 슬롯에서 로드, 직렬화 X
		std::vector<MaterialAssetHandle> m_Materials;        // 오버라이드
		std::string m_MeshPath;
		std::vector<std::string> m_MaterialNames;           // 오버라이드 이름
	};
}
