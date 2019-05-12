#include "OceanSimulation.h"
#include "../Clock.h"
#include "../buffer/Buffer.h"

#include <vector>

namespace Atlas {

	namespace Renderer {

		namespace GPGPU {

			OceanSimulation::OceanSimulation(int32_t N, int32_t L) : N(N), L(L) {

				noise0 = Texture::Texture2D(N, N, AE_R8);
				noise1 = Texture::Texture2D(N, N, AE_R8);
				noise2 = Texture::Texture2D(N, N, AE_R8);
				noise3 = Texture::Texture2D(N, N, AE_R8);

				twiddleIndices = Texture::Texture2D((int32_t)log2((float)N), N, AE_RGBA32F);

				Helper::NoiseGenerator::GenerateNoiseTexture2D(noise0);
				Helper::NoiseGenerator::GenerateNoiseTexture2D(noise1);
				Helper::NoiseGenerator::GenerateNoiseTexture2D(noise2);
				Helper::NoiseGenerator::GenerateNoiseTexture2D(noise3);

				displacementMap = Texture::Texture2D(N, N, AE_RGBA32F, GL_REPEAT, GL_LINEAR);
				normalMap = Texture::Texture2D(N, N, AE_RGBA16F, GL_REPEAT, GL_LINEAR);

				h0K = Texture::Texture2D(N, N, AE_RGBA32F);
				h0MinusK = Texture::Texture2D(N, N, AE_RGBA32F);

				hTDy = Texture::Texture2D(N, N, AE_RGBA32F);
				hTDx = Texture::Texture2D(N, N, AE_RGBA32F);
				hTDz = Texture::Texture2D(N, N, AE_RGBA32F);

				hTDyPingpong = Texture::Texture2D(N, N, AE_RGBA32F);
				hTDxPingpong = Texture::Texture2D(N, N, AE_RGBA32F);
				hTDzPingpong = Texture::Texture2D(N, N, AE_RGBA32F);

				h0.AddStage(AE_COMPUTE_STAGE, "ocean/h0.csh");
				ht.AddStage(AE_COMPUTE_STAGE, "ocean/ht.csh");
				twiddle.AddStage(AE_COMPUTE_STAGE, "ocean/twiddleIndices.csh");
				butterfly.AddStage(AE_COMPUTE_STAGE, "ocean/butterfly.csh");
				inversion.AddStage(AE_COMPUTE_STAGE, "ocean/inversion.csh");
				normal.AddStage(AE_COMPUTE_STAGE, "ocean/normal.csh");

				htNUniform = ht.GetUniform("N");
				htLUniform = ht.GetUniform("L");
				htTimeUniform = ht.GetUniform("time");

				butterflyStageUniform = butterfly.GetUniform("stage");
				butterflyPingpongUniform = butterfly.GetUniform("pingpong");
				butterflyDirectionUniform = butterfly.GetUniform("direction");

				inversionNUniform = inversion.GetUniform("N");
				inversionPingpongUniform = inversion.GetUniform("pingpong");

				normalNUniform = normal.GetUniform("N");

				ComputeTwiddleIndices();
				ComputeH0();

			}

			void OceanSimulation::SetState(float waveAmplitude, vec2 waveDirection,
				float windSpeed, float windDependency) {				

				this->waveAmplitude = waveAmplitude;
				this->waveDirection = waveDirection;
				this->windSpeed = windSpeed;
				this->windDependency = windDependency;

			}

			void OceanSimulation::ComputeH0() {

				auto NUniform = h0.GetUniform("N");
				auto LUniform = h0.GetUniform("L");
				auto AUniform = h0.GetUniform("A");
				auto wUniform = h0.GetUniform("w");
				auto windspeedUniform = h0.GetUniform("windspeed");
				auto windDependencyUniform = h0.GetUniform("windDependency");

				h0.Bind();

				NUniform->SetValue(N);
				LUniform->SetValue(L);
				AUniform->SetValue(waveAmplitude);
				wUniform->SetValue(waveDirection);
				windspeedUniform->SetValue(windSpeed);
				windDependencyUniform->SetValue(windDependency);

				noise0.Bind(GL_TEXTURE2);
				noise1.Bind(GL_TEXTURE3);
				noise2.Bind(GL_TEXTURE4);
				noise3.Bind(GL_TEXTURE5);

				h0K.Bind(GL_WRITE_ONLY, 0);
				h0MinusK.Bind(GL_WRITE_ONLY, 1);

				glDispatchCompute(N / 16, N / 16, 1);

			}

			void OceanSimulation::ComputeTwiddleIndices() {

				auto nUniform = twiddle.GetUniform("N");

				auto bitCount = (int32_t)log2f((float)N);
				std::vector<int32_t> indices(N);

				for (int32_t i = 0; i < N; i++) {
					indices[i] = ReverseBits(i, bitCount);
				}

				Buffer::Buffer buffer(AE_SHADER_BUFFER, sizeof(int32_t), AE_BUFFER_DYNAMIC_STORAGE);
				buffer.SetSize(indices.size());
				buffer.SetData(indices.data(), 0, indices.size());

				nUniform->SetValue(N);

				twiddleIndices.Bind(GL_WRITE_ONLY, 0);
				buffer.BindBase(1);

				glDispatchCompute(bitCount, N / 16, 1);

			}

			void OceanSimulation::Compute() {

				ht.Bind();

				htNUniform->SetValue(N);
				htLUniform->SetValue(L);
				htTimeUniform->SetValue(5.0f * Clock::Get());
				// htTimeUniform->SetValue(.05f * Clock::Get());

				hTDy.Bind(GL_WRITE_ONLY, 0);
				hTDx.Bind(GL_WRITE_ONLY, 1);
				hTDz.Bind(GL_WRITE_ONLY, 2);

				h0K.Bind(GL_READ_ONLY, 3);
				h0MinusK.Bind(GL_READ_ONLY, 4);

				glDispatchCompute(N / 16, N / 16, 1);

				int32_t pingpong = 0;
				int32_t log2n = (int32_t)log2f((float)N);

				butterfly.Bind();

				twiddleIndices.Bind(GL_READ_ONLY, 0);

				// Horizontal
				butterflyDirectionUniform->SetValue(0);

				hTDy.Bind(GL_READ_WRITE, 1);
				hTDx.Bind(GL_READ_WRITE, 2);
				hTDz.Bind(GL_READ_WRITE, 3);

				hTDyPingpong.Bind(GL_READ_WRITE, 4);
				hTDxPingpong.Bind(GL_READ_WRITE, 5);
				hTDzPingpong.Bind(GL_READ_WRITE, 6);

				for (int32_t i = 0; i < log2n; i++) {
					butterflyStageUniform->SetValue(i);
					butterflyPingpongUniform->SetValue(pingpong);

					glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
						GL_SHADER_STORAGE_BARRIER_BIT);

					glDispatchCompute(N / 16, N / 16, 1);

					pingpong = (pingpong + 1) % 2;
				}

				// Vertical
				butterflyDirectionUniform->SetValue(1);

				for (int32_t i = 0; i < log2n; i++) {
					butterflyStageUniform->SetValue(i);
					butterflyPingpongUniform->SetValue(pingpong);

					glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
						GL_SHADER_STORAGE_BARRIER_BIT);

					glDispatchCompute(N / 16, N / 16, 1);

					pingpong = (pingpong + 1) % 2;
				}

				// Inverse and correct the texture
				inversion.Bind();

				inversionNUniform->SetValue(N);
				inversionPingpongUniform->SetValue(pingpong);

				displacementMap.Bind(GL_WRITE_ONLY, 0);

				glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
					GL_SHADER_STORAGE_BARRIER_BIT);

				glDispatchCompute(N / 16, N / 16, 1);

				// Calculate normals
				normal.Bind();

				normalNUniform->SetValue(N);

				displacementMap.Bind(GL_READ_ONLY, 0);
				normalMap.Bind(GL_WRITE_ONLY, 1);

				glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
					GL_SHADER_STORAGE_BARRIER_BIT);

				glDispatchCompute(N / 16, N / 16, 1);

			}

			int32_t OceanSimulation::ReverseBits(int32_t data, int32_t bitCount) {

				int32_t reversed = 0;

				for (int32_t i = 0; i < bitCount; i++) {
					if (data & (1 << i))
						reversed |= (1 << ((bitCount - 1) - i));
				}

				return reversed;

			}

		}

	}

}