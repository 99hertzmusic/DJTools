#pragma once
// 99Hertz plugin licences. Shared by every Pro plugin: copy this folder (Licence.h/.cpp + tweetnacl).
//
// A key is "99HZ-<base64url(json)>.<base64url(ed25519 signature)>", json = {p, e, t, i, x}:
//   p product id ("autopresspro", "autochainpro", "djtools", ...) or "all"   e email   t "pro" | "trial"   i issued   x expiry (0 = never)
// Keys are checked offline against the public key below, so there's no account, server check or iLok.
// Keys are made by 99Hertz's private signing tool (and by the site when someone buys or starts a trial).

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>

namespace hz
{
struct LicenceInfo
{
    enum Kind { none, trial, pro, trialExpired, invalid };
    Kind kind = none;
    juce::String email, product;
    juce::int64 expires = 0;   // unix seconds, 0 = never

    bool unlocked() const { return kind == trial || kind == pro; }
    int daysLeft() const;
};

class Licence
{
public:
    explicit Licence (juce::String productId) : product (std::move (productId)) {}

    // Reads the saved key (if any) and checks it.
    LicenceInfo check() const;

    // Checks a pasted key; saves it if it's valid for this product.
    LicenceInfo apply (const juce::String& key) const;

    static LicenceInfo verify (const juce::String& key, const juce::String& productId);

    // Asks 99hertz.com for a 7-day trial key for this email. `done` runs on the message thread.
    void requestTrial (const juce::String& email, bool subscribe,
                       std::function<void (LicenceInfo, juce::String error)> done) const;

    juce::File keyFile() const;
    const juce::String product;

    static constexpr const char* trialUrl = "https://99hertz.com/api/plugin/trial.php";
};
} // namespace hz
