#include "kart_state_hash.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace kartgame {

void KartStateHash::_bind_methods() {
	ClassDB::bind_method(D_METHOD("reset"), &KartStateHash::reset);
	ClassDB::bind_method(D_METHOD("set_quantum", "quantum"), &KartStateHash::set_quantum);
	ClassDB::bind_method(D_METHOD("get_quantum"), &KartStateHash::get_quantum);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "quantum"), "set_quantum", "get_quantum");

	ClassDB::bind_method(D_METHOD("add_float", "value"), &KartStateHash::add_float);
	ClassDB::bind_method(D_METHOD("add_int", "value"), &KartStateHash::add_int);
	ClassDB::bind_method(D_METHOD("add_vector3", "value"), &KartStateHash::add_vector3);
	ClassDB::bind_method(D_METHOD("add_transform", "value"), &KartStateHash::add_transform);

	ClassDB::bind_method(D_METHOD("hex"), &KartStateHash::hex);
	ClassDB::bind_method(D_METHOD("digest"), &KartStateHash::digest);
}

void KartStateHash::reset() {
	hash_ = kart::core::StateHash(quantum_);
}

void KartStateHash::set_quantum(double p_quantum) {
	// A zero or negative grid would divide by zero inside the core class. Clamped
	// rather than asserted, because this is reachable from a tuning UI.
	quantum_ = p_quantum > 0.0 ? p_quantum : kart::core::StateHash::DEFAULT_QUANTUM;
	reset();
}

double KartStateHash::get_quantum() const {
	return quantum_;
}

void KartStateHash::add_float(double p_value) {
	hash_.add_double(p_value);
}

void KartStateHash::add_int(int64_t p_value) {
	hash_.add_int(p_value);
}

void KartStateHash::add_vector3(const Vector3 &p_value) {
	hash_.add_vector3(p_value.x, p_value.y, p_value.z);
}

void KartStateHash::add_transform(const Transform3D &p_value) {
	hash_.add_vector3(p_value.origin.x, p_value.origin.y, p_value.origin.z);
	for (int column = 0; column < 3; ++column) {
		const Vector3 axis = p_value.basis.get_column(column);
		hash_.add_vector3(axis.x, axis.y, axis.z);
	}
}

String KartStateHash::hex() const {
	return String::num_uint64(hash_.digest(), 16).pad_zeros(16);
}

int64_t KartStateHash::digest() const {
	return static_cast<int64_t>(hash_.digest());
}

} // namespace kartgame
