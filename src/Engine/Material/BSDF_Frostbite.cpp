#include <Engine/BSDF_Frostbite.h>

#include <Basic/Image.h>
#include <Basic/Math.h>
#include <Basic/CosineWeightedHemisphereSampler3D.h>

using namespace CppUtil;
using namespace CppUtil::Engine;
using namespace CppUtil::Basic;
using namespace std;

const Ubpa::rgbf BSDF_Frostbite::Fr(const Ubpa::normalf & w, const Ubpa::normalf & h, const Ubpa::rgbf & albedo, float metallic) {
	// Schlick��s approximation
	// use a Spherical Gaussian approximation to replace the power.
	//  slightly more efficient to calculate and the difference is imperceptible

	const Ubpa::rgbf F0 = Ubpa::rgbf(0.04f).lerp(albedo, metallic);
	float HoWi = h.dot(w);
	return F0 + pow(2.0f, (-5.55473f * HoWi - 6.98316f) * HoWi) * (Ubpa::rgbf(1.0f) - F0);
}

const float BSDF_Frostbite::Fr_DisneyDiffuse(const Ubpa::normalf & wo, const Ubpa::normalf & wi, float linearRoughness) {
	auto h = (wo + wi).normalize();
	float HoWi = h.dot(wi);
	float HoWi2 = HoWi * HoWi;

	float NoWo = SurfCoord::CosTheta(wo);
	float NoWi = SurfCoord::CosTheta(wi);

	float energyBias = Math::Lerp(0.f, 0.5f, linearRoughness);
	float energyFactor = Math::Lerp(1.f, 1.f / 1.51f, linearRoughness);
	float fd90 = energyBias + 2.f * HoWi2 * linearRoughness;
	float lightScatter = 1.f + fd90 * pow(1.f - NoWi, 5);
	float viewScatter = 1.f + fd90 * pow(1.f - NoWo, 5);

	return lightScatter * viewScatter * energyFactor;
}

const Ubpa::rgbf BSDF_Frostbite::F(const Ubpa::normalf & wo, const Ubpa::normalf & wi, const Ubpa::pointf2 & texcoord) {
	auto albedo = GetAlbedo(texcoord);
	auto metallic = GetMetallic(texcoord);
	auto roughness = GetRoughness(texcoord);
	float perpRoughness = roughness * roughness;
	//auto ao = GetAO(texcoord);

	ggx.SetAlpha(perpRoughness);

	auto wh = (wo + wi).normalize();

	// renormalized Disney
	auto diffuse = albedo / Ubpa::PI<float> * Fr_DisneyDiffuse(wo, wi, roughness);

	auto D = ggx.D(wh);
	auto G = ggx.G(wo, wi, wh);
	auto F = Fr(wo, wh, albedo, metallic);
	float denominator = 4.f * abs(SurfCoord::CosTheta(wo) * SurfCoord::CosTheta(wi));
	if (denominator == 0) // ���ٷ��������Է���������Ȼ���ܲ������ţ����߼�˳��Щ
		return Ubpa::rgbf(0.f);
	auto specular = D * G * F / denominator;

	auto rst = (1 - metallic) * diffuse + specular;

	return rst;
}

// probability density function
float BSDF_Frostbite::PDF(const Ubpa::normalf & wo, const Ubpa::normalf & wi, const Ubpa::pointf2 & texcoord) {
	auto roughness = GetRoughness(texcoord);
	float perpRoughness = roughness * roughness;
	ggx.SetAlpha(perpRoughness);

	auto metallic = GetMetallic(texcoord);
	auto pSpecular = 1 / (2 - metallic);

	auto wh = wo + wi;
	if (wh.is_all_zero())
		return 0;

	wh.normalize_self();

	float pdDiffuse = SurfCoord::CosTheta(wi) * Math::INV_PI;
	float pdSpecular = ggx.PDF(wh) / (4.f*abs(wo.dot(wh))); // ���ݼ��ι�ϵ�Լ�ǰ�ߵ��� 0 ��������������� 0
	return Math::Lerp(pdDiffuse, pdSpecular, pSpecular);
}

// PD is probability density
// return albedo
const Ubpa::rgbf BSDF_Frostbite::Sample_f(const Ubpa::normalf & wo, const Ubpa::pointf2 & texcoord, Ubpa::normalf & wi, float & PD) {
	auto roughness = GetRoughness(texcoord);
	float perpRoughness = roughness * roughness;
	ggx.SetAlpha(perpRoughness);

	// ���� metallic ���ֱ����
	Ubpa::normalf wh;
	auto metallic = GetMetallic(texcoord);
	auto pSpecular = 1 / (2 - metallic);
	if (Math::Rand_F() < pSpecular) {
		wh = ggx.Sample_wh();
		wi = Ubpa::normalf::reflect(-wo, wh);
	}
	else {
		CosineWeightedHemisphereSampler3D sampler;
		wi = sampler.GetSample().cast_to<Ubpa::normalf>();
		wh = (wo + wi).normalize();
	}

	if (SurfCoord::CosTheta(wi) <= 0) {
		PD = 0;
		return Ubpa::rgbf(0.f);
	}

	float pdDiffuse = SurfCoord::CosTheta(wi) * Math::INV_PI;
	float pdSpecular = ggx.PDF(wh) / (4.f*abs(wo.dot(wh)));
	PD = Math::Lerp(pdDiffuse, pdSpecular, pSpecular);

	auto albedo = GetAlbedo(texcoord);
	auto diffuse = albedo * Math::INV_PI * Fr_DisneyDiffuse(wo, wi, roughness);

	auto D = ggx.D(wh);
	auto G = ggx.G(wo, wi, wh);
	auto F = Fr(wo, wh, albedo, metallic);
	float denominator = 4.f * abs(SurfCoord::CosTheta(wo) * SurfCoord::CosTheta(wi));
	if (denominator == 0) {// ���ٷ��������Է���������Ȼ���ܲ������ţ����߼�˳��Щ
		PD = 0;
		wi = Ubpa::normalf(0.f);
		return Ubpa::rgbf(0.f);
	}

	auto specular = D * G * F / denominator;

	auto kS = F;
	auto kD = (1 - metallic) * (Ubpa::rgbf(1.f) - kS);

	auto rst = kD * diffuse + specular;

	//auto ao = GetAO(texcoord);
	return rst;
}

void BSDF_Frostbite::ChangeNormal(const Ubpa::pointf2 & texcoord, const Ubpa::normalf & tangent, Ubpa::normalf & normal) const {
	if (!normalTexture || !normalTexture->IsValid())
		return;

	const auto rgb = normalTexture->Sample(texcoord, Image::Mode::BILINEAR).to_rgb();
	Ubpa::normalf tangentSpaceNormal = 2.f * rgb.cast_to<Ubpa::normalf>() - Ubpa::normalf(1.f);

	normal = TangentSpaceNormalToWorld(tangent, normal, tangentSpaceNormal);
}

const Ubpa::rgbf BSDF_Frostbite::GetAlbedo(const Ubpa::pointf2 & texcoord) const {
	if (!albedoTexture || !albedoTexture->IsValid())
		return colorFactor;

	return colorFactor * albedoTexture->Sample(texcoord, Image::Mode::BILINEAR).to_rgb();
}

float BSDF_Frostbite::GetMetallic(const Ubpa::pointf2 & texcoord) const {
	if (!metallicTexture || !metallicTexture->IsValid())
		return metallicFactor;

	return metallicFactor * metallicTexture->Sample(texcoord, Image::Mode::BILINEAR)[0];
}

float BSDF_Frostbite::GetRoughness(const Ubpa::pointf2 & texcoord) const {
	if (!roughnessTexture || !roughnessTexture->IsValid())
		return roughnessFactor;

	return roughnessFactor * roughnessTexture->Sample(texcoord, Image::Mode::BILINEAR)[0];
}

float BSDF_Frostbite::GetAO(const Ubpa::pointf2 & texcoord) const {
	if (!aoTexture || !aoTexture->IsValid())
		return 1.0f;

	return aoTexture->Sample(texcoord, Image::Mode::BILINEAR)[0];
}