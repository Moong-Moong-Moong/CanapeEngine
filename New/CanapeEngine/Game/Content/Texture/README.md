# 샘플 씬 BaseColor 텍스처

- `Player_BaseColor.png`: Player.fbx의 청색·금색 갑옷, 가죽, 피부, 검을 위한 테스트 아틀라스.
- `Enemy_BaseColor.png`: Enemy.fbx의 녹색 돌연변이 피부, 갈색 하의, 뼈 발톱을 위한 테스트 아틀라스.

두 이미지는 실제 FBX의 폴리곤 코너 UV와 정점 위치에서 추출한 색상 영역 가이드를 참조하여 Codex의 내장 image_gen 도구로 생성했습니다. 기존 UV 방향을 유지하며 Player의 음수 U 좌표는 Renderer의 Wrap 샘플링으로 처리합니다. 원본 캐릭터의 공식 텍스처를 복원한 것이 아닌 렌더링 확인용 자산입니다. 정밀한 이음새 보정과 최종 아트 검수는 별도로 필요합니다.

SampleScene.cpp에서 ResourceManager.LoadTexture()로 로드하여 각 모델의 실제 Material Slot에 지정합니다. Player의 몸과 검은 Material Slot 0과 같은 아틀라스를 공유합니다. 빌드 후 기존 Content 복사 이벤트를 통해 실행 파일 옆 Content/Texture에도 복사됩니다.

포맷은 불투명 PNG, RGB는 sRGB 색상 데이터입니다. BaseColor는 흰색이며 기존 조명·출력 타깃·카메라 설정은 유지합니다. 이미지 교체 후 실행 중 자동 갱신은 없으므로 게임을 다시 실행하세요.

## 생성 프롬프트

공통 입력: 해당 모델의 UV 색상 가이드와 텍스처 미지정 샘플 씬의 실제 렌더링 화면. 내장 image_gen 편집 모드, 불투명 배경을 사용했습니다.

### Player

Use case: stylized-concept. Asset type: game character BaseColor UV texture atlas, 1024x1024 square opaque PNG. Image 1 is an EXACT UV color guide extracted from Player.fbx. Image 2 is the rendered untextured knight and mutant reference, use ONLY the foreground knight for subject context. Edit Image 1 into a finished fantasy knight BaseColor texture atlas. Preserve every UV island silhouette, boundary, placement, scale, orientation, and color-region assignment EXACTLY; never repack, rotate, mirror, flip vertically, or add a rendered character. Blue regions become medium-bright cobalt blue painted steel armor with restrained fine scratches and small silver rivets; ochre regions become warm gold brass trim with fine engraving; beige strip on far left becomes bright silver sword blade; peach regions become warm natural skin; dark slate regions become charcoal leather cloth and boots. Provide subtle broad surface variation readable at game distance. Skin should be clean with only subtle details; don't invent eyes or faces when orientation is uncertain. Match blue and gold palette across islands, pad island colors slightly into surrounding background to avoid dark seams. Flat diffuse albedo, uniform illumination, no dramatic highlights, shadows, normal map, transparency, grid, text, labels, UV wireframe, logos, watermark. The output must be the full square texture atlas only, matching Image 1's pixel-space layout.

### Enemy

Use case: stylized-concept. Asset type: game mutant BaseColor UV texture atlas, 1024x1024 square opaque PNG. Image 1 is an EXACT UV color guide extracted from Enemy.fbx. Image 2 is rendered untextured sample scene, use ONLY the background muscular mutant for subject context. Edit Image 1 into a finished fantasy mutant BaseColor texture atlas. Preserve every UV island silhouette, placement, scale, orientation, and broad region assignment EXACTLY; do not repack, rotate, mirror, vertically flip, or add a rendered character. Green regions become medium-bright moss green tough mutant skin with subtle mottling, sparse old scars, small leathery pores and restrained ochre variation; brown regions become dark brown rugged trousers or wraps; beige regions become pale ivory bone claws with muted darker roots. Keep skin pattern fine and continuous, do not paint independent full limbs or a separate character illustration on the islands. If facial island orientation is uncertain use skin detail rather than guessed facial features. Provide coherent surface variation readable at game distance. Slight color padding into adjacent background to avoid seams. Flat uniformly lit diffuse albedo only, no baked scene shadows, specular highlights, normal map, grid, text, labels, UV wireframe, transparency, watermark. Output the full square texture atlas only, exactly matching Image 1's pixel-space layout.
