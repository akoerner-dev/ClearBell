// cb_labels.h - every label that separates the HMAC purposes of one key (security catalogue RV-59). The code uses
// these constants; test_pairing.cpp checks on this very list that no label is a prefix of another and none starts
// like the frame domain. Rules for a new label: compile-time constant, add it to the list, never a prefix of an
// existing one. Changing a label breaks the pairing of units in the field (protocol v2) - only with a new version.
#pragma once

#include <cstddef>

namespace cb {

// Keyed with the LINK key (one per flat).
constexpr const char *kLabelFrame = "CBv2";       // frame tag: "CBv2" | frame bytes (cb_protocol)
constexpr const char *kLabelPhone = "phone";      // stream for the phone key in a PhoneAdd event (cb_phones)
constexpr const char *kLabelConfirm = "confirm|";  // key confirmation of the indoor unit at pairing
constexpr const char *kLabelWrap = "wrap|";       // tag of wrap32
constexpr const char *kLinkKeyLabels[] = {kLabelFrame, kLabelPhone, kLabelConfirm, kLabelWrap};
constexpr size_t kLinkKeyLabelCount = sizeof(kLinkKeyLabels) / sizeof(kLinkKeyLabels[0]);

// Keyed with the key derived from the indoor unit's DEVICE CODE (pairing offer).
constexpr const char *kLabelCode = "CBP1";  // HMAC(device code, "CBP1") = the offer key
constexpr const char *kLabelEnc = "enc";    // stream that encrypts the link key in the offer
constexpr const char *kLabelTag = "tag|";   // tag of the offer
constexpr const char *kCodeKeyLabels[] = {kLabelCode, kLabelEnc, kLabelTag};
constexpr size_t kCodeKeyLabelCount = sizeof(kCodeKeyLabels) / sizeof(kCodeKeyLabels[0]);

}  // namespace cb
