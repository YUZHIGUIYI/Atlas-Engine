#pragma once

#include "MeshActor.h"

namespace Atlas {

    namespace Actor {

        class MovableMeshActor : public MeshActor {

        public:
            MovableMeshActor() : MeshActor(ResourceHandle<Mesh::Mesh>()) {}

            explicit MovableMeshActor(ResourceHandle<Mesh::Mesh> mesh, mat4 matrix = mat4(1.0f))
                : MeshActor(mesh) { this->SetMatrix(matrix); }

        };

    }

}