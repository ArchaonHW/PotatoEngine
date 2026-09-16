#!/bin/bash
# Syntax-check all engine .cpp files; print errors grouped by file.
cd "$(dirname "$0")/.." || exit 1
export PATH="$PATH:/c/Users/potat/scoop/apps/mingw/current/bin"
GLFW=/c/Users/potat/scoop/apps/glfw/current
FILES="
AI/AIAgentSystem.cpp AI/AgentChain.cpp AI/IntelligentDevelopmentSystem.cpp
AI/LLMIntegration.cpp AI/NaturalLanguageProcessing.cpp AI/NeuralNetwork.cpp
AI/RAGSystem.cpp AI/ReinforcementLearning.cpp AI/ToolFramework.cpp
Audio/AudioSystem.cpp Core/PotatoEngine.cpp
ECS/Component.cpp ECS/ECSCoordinator.cpp ECS/Entity.cpp ECS/System.cpp
Events/EventBus.cpp FileSystem/FileSystem.cpp GameObject/GameObject.cpp
Input/InputManager.cpp Logging/Logger.cpp Memory/MemoryManager.cpp
Physics/PhysicsSystem.cpp Platform/PlatformSystem.cpp
Rendering/Camera.cpp Rendering/Lighting.cpp Rendering/ModelLoader.cpp
Rendering/OpenGLRenderer.cpp Rendering/Shader.cpp
Resources/ResourceManager.cpp Scene/SceneNode.cpp Security/SecuritySystem.cpp
Serialization/Serialization.cpp Time/TimeManager.cpp GUI/AgentGUI.cpp
"
total=0
for f in $FILES; do
  out=$(g++ -std=c++20 -fsyntax-only -DGLFW_INCLUDE_NONE -include Examples/compat_log.h \
    -I. -Iexternal/glad_gen/include -I"$GLFW/include" "$f" 2>&1 | grep -E "error:")
  if [ -n "$out" ]; then
    n=$(echo "$out" | wc -l)
    total=$((total + n))
    echo "=== $f ($n errors) ==="
    echo "$out" | head "${MAXERR:-6}"
  fi
done
echo "=== TOTAL: $total errors ==="
