#pragma once

#include "../System.h"

#include "Vignette.h"
#include "ChromaticAberration.h"
#include "Sharpen.h"
#include "FilmGrain.h"
#include "TAA.h"

namespace Atlas {

    namespace PostProcessing {

        class PostProcessing {

        public:
            float saturation = 1.0f;
            float contrast = 1.0f;
            float whitePoint = 10.0f;

            bool filmicTonemapping = false;

            TAA taa;
            Vignette vignette;
            ChromaticAberration chromaticAberration;
            FilmGrain filmGrain;
            Sharpen sharpen;

        };

    }

}