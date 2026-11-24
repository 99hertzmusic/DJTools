#pragma once
// 99Hertz plugin licences. Shared by every Pro plugin: copy this folder (Licence.h/.cpp + tweetnacl).
// Keep Licence.h / Licence.cpp byte-identical across the plugins.
//
// A key is "99HZ-<base64url(json)>.<base64url(ed25519 signature)>", json = {p, e, t, i, x}:
//   p product id ("autopresspro", "autochainpro", ...) or "all"   e email   t "pro" | "trial"   i issued   x expiry (0 = never)
// Keys are checked offline against the public key below, so there's no account, server check or iLok.
// Keys are made by 99Hertz's private signing tool (and by the site when someone buys or starts a trial).

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>

namespace hz
{
struct LicenceInfo
{
    // saveFailed: only ever returned by Licence::apply / requestTrial, when a good key couldn't be written
    // to (or read back from) the licence folder.
    enum Kind { none, trial, pro, trialExpired, invalid, saveFailed };
    Kind kind = none;
    juce::String email, product;
    juce::int64 expires = 0;   // unix seconds, 0 = never

    bool unlocked() const { return kind == trial || kind == pro; }
    int daysLeft() const;

    // Shared wording for every plugin's licence button / panel:
    //   "UPGRADE"  ·  "TRIAL · 5 DAYS LEFT · UPGRADE"  ·  "TRIAL ENDED · UPGRADE"  ·  "LICENSED"
    juce::String buttonText() const;
    juce::String daysLeftText() const;   // "5 DAYS LEFT" / "1 DAY LEFT"

    bool sameAs (const LicenceInfo& o) const
    {
        return kind == o.kind && email == o.email && product == o.product && expires == o.expires;
    }
};

class Licence
{
public:
    explicit Licence (juce::String productId) : product (std::move (productId)) {}

    // Reads the saved key (if any) and checks it.
    LicenceInfo check() const;

    // Checks a pasted key; saves it if it's valid for this product. Never replaces an unlocked key
    // (pro, or a trial that's still running) with a locked one, and never replaces pro with a trial:
    // in those cases the current licence comes back. kind == saveFailed if the key couldn't be saved.
    LicenceInfo apply (const juce::String& key) const;

    // Pasted text is cleaned first: anything before the first "99HZ-", any character that can't be
    // part of a key (spaces of every kind, quotes, ...) and trailing full stops are dropped.
    static LicenceInfo verify (const juce::String& key, const juce::String& productId);
    static juce::String cleanKey (const juce::String& pasted);

    // Asks 99hertz.com for a 7-day trial key for this email and saves it. `done` runs on the message thread.
    void requestTrial (const juce::String& email, bool subscribe,
                       std::function<void (LicenceInfo, juce::String error)> done) const;

    juce::File keyFile() const;
    const juce::String product;

    static constexpr const char* trialUrl = "https://99hertz.com/api/plugin/trial.php";
};

// Keeps an open plugin's licence current. Every ~2 s it looks at the key file and re-checks it if the
// file appeared, went or changed (another instance unlocked, or a trial request finished after its
// panel closed); once a minute it re-checks regardless, so a trial running out mid-session is noticed.
// onChange runs on the message thread, and only when the result (or its days left) differs.
class LicenceWatcher : private juce::Timer
{
public:
    LicenceWatcher (const Licence& l, LicenceInfo current, std::function<void (const LicenceInfo&)> onChange,
                    int pollMs = 2000, int fullCheckMs = 60000);
    ~LicenceWatcher() override;

    // One poll now (what the timer does). force = re-check even if the file looks unchanged.
    void poll (bool force = false);

private:
    void timerCallback() override { poll(); }
    void stamp();

    const Licence& licence;
    std::function<void (const LicenceInfo&)> onChange;
    LicenceInfo last;
    juce::int64 fileTime = 0, fileSize = -1;
    juce::uint32 lastFull = 0;
    int lastDays = 0;
    const int fullMs;
};
} // namespace hz
