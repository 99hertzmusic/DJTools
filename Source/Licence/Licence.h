#pragma once
// 99Hertz plugin licences. Shared by every Pro plugin: copy this folder (Licence.h/.cpp + tweetnacl).
// Keep Licence.h / Licence.cpp byte-identical across the plugins. Needs juce_cryptography (SHA-256).
//
// A key is "99HZ-<base64url(json)>.<base64url(ed25519 signature)>", json = {p, e, t, i, x[, o]}:
//   p product id ("autopresspro", "autochainpro", ...) or "all"   e email   t "pro" | "trial"   i issued
//   x expiry (0 = never)   o order number (purchase keys only)
// Keys are checked offline against the public key in Licence.cpp. Trial keys work as soon as they're saved.
//
// Pro keys also need this computer activated, once (knowledge/plugin-activation-spec.md): unlocking a
// pro key asks 99hertz.com/api/plugin/activate.php for a token, "99HA-<base64url(json)>.<sig>" with
// json = {k: sha256(key), m: machine id, p: product or "all", i}, signed with the same key. The token is
// saved next to the key as <product>.act (all.act for a bundle key, which then covers every plugin on
// this computer). After that everything is offline. A pro key without a valid token for this computer
// is needsActivation: the plugin plays as free and the panel offers ACTIVATE THIS COMPUTER.

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>
#include <memory>

namespace hz
{
struct LicenceInfo
{
    // saveFailed: only ever returned by Licence::apply / requestTrial / unlock / activate, when a good
    // key couldn't be written to (or read back from) the licence folder.
    // needsActivation: a good pro key, but this computer isn't activated for it (no valid token).
    enum Kind { none, trial, pro, trialExpired, invalid, saveFailed, needsActivation };
    Kind kind = none;
    juce::String email, product;
    juce::int64 expires = 0;   // unix seconds, 0 = never
    juce::int64 order = 0;     // order number from the key, 0 = none

    bool unlocked() const { return kind == trial || kind == pro; }
    bool isProKey() const { return kind == pro || kind == needsActivation; }   // a bought key, activated or not
    int daysLeft() const;

    // Shared wording for every plugin's licence button / panel:
    //   "UPGRADE"  ·  "TRIAL · 5 DAYS LEFT · UPGRADE"  ·  "TRIAL ENDED · UPGRADE"  ·  "LICENSED"
    //   ·  "ACTIVATE" (a pro key this computer isn't activated for; the panel then offers ACTIVATE THIS COMPUTER)
    juce::String buttonText() const;
    juce::String daysLeftText() const;   // "5 DAYS LEFT" / "1 DAY LEFT"
    juce::String ownerText() const;      // "buyer@example.com · ORDER #1234" (no order part if the key has none)
    juce::String licensedText() const;   // "LICENSED TO BUYER@EXAMPLE.COM · ORDER #1234"

    bool sameAs (const LicenceInfo& o) const
    {
        return kind == o.kind && email == o.email && product == o.product && expires == o.expires && order == o.order;
    }
};

namespace detail { struct LicenceNet; }

class Licence
{
public:
    explicit Licence (juce::String productId);
    Licence (const Licence&);
    ~Licence();

    // Reads the saved key (if any) and checks it, including this computer's activation for a pro key.
    LicenceInfo check() const;

    // Checks a pasted key and saves it if it's valid for this product. No network: a pro key comes back
    // as needsActivation unless this computer already has a token for it (the panel uses unlock()).
    // Never replaces an unlocked key (pro, or a trial that's still running) with a locked one, and never
    // replaces a pro key with a trial: in those cases the current licence comes back.
    // kind == saveFailed if the key couldn't be saved.
    LicenceInfo apply (const juce::String& key) const;

    // Pasted text is cleaned first: anything before the first "99HZ-", any character that can't be
    // part of a key (spaces of every kind, quotes, ...) and trailing full stops are dropped.
    // verify() looks at the key alone: a good pro key is `pro` here even if this computer isn't activated.
    static LicenceInfo verify (const juce::String& key, const juce::String& productId);
    static juce::String cleanKey (const juce::String& pasted);

    // Is `token` a genuine activation of `key` for `productId` on the computer with machine id `mid`?
    static bool tokenMatches (const juce::String& token, const juce::String& key, const juce::String& productId,
                              const juce::String& mid);

    // The calls below that talk to 99hertz.com run on a background thread (12 s timeout); `done` runs
    // later on the message thread. Destroying the last Licence of this plugin (closing it) cancels any
    // request in flight and waits for it, so no thread outlives the plugin; `done` may still run after
    // the panel that asked is gone, so capture a SafePointer.
    using Callback = std::function<void (LicenceInfo, juce::String error)>;

    // Asks 99hertz.com for a 7-day trial key for this email and saves it.
    void requestTrial (const juce::String& email, bool subscribe, Callback done) const;

    // The panel's UNLOCK: a trial key is saved straight away; a pro key is activated for this computer
    // first and only saved (with its token) when that works. `done` runs before this returns when no
    // request is needed (bad key, trial key, or this computer already has a token for the key).
    void unlock (const juce::String& key, Callback done) const;

    // ACTIVATE THIS COMPUTER: activates the saved pro key.
    void activate (Callback done) const;

    // MOVE TO ANOTHER COMPUTER: releases this computer on the server, then deletes the token (the key
    // stays, so re-activating anywhere is just pasting it). Nothing changes if the server can't be reached.
    void deactivate (Callback done) const;

    juce::File keyFile() const;
    juce::File tokenFile() const;     // <product>.act
    juce::File allTokenFile() const;  // all.act (bundle keys)
    const juce::String product;

    // This computer: first 32 hex chars of sha256("99hz|" + unique device id). Never empty.
    static juce::String machineId();
    static juce::String machineName();   // computer name sent with an activation (max 64 chars)

    static juce::String serverBase();    // "https://99hertz.com/api/plugin/"
    static void setServerBaseForTests (const juce::String& base);
    static int requestsInFlight();

private:
    std::shared_ptr<detail::LicenceNet> net;   // shared by every Licence of this plugin binary
};

// Keeps an open plugin's licence current. Every ~2 s it looks at the key and activation files and
// re-checks if one appeared, went or changed (another instance unlocked or moved the licence, or a
// request finished after its panel closed); once a minute it re-checks regardless, so a trial running
// out mid-session is noticed. onChange runs on the message thread, and only when the result (or its
// days left) differs.
class LicenceWatcher : private juce::Timer
{
public:
    LicenceWatcher (const Licence& l, LicenceInfo current, std::function<void (const LicenceInfo&)> onChange,
                    int pollMs = 2000, int fullCheckMs = 60000);
    ~LicenceWatcher() override;

    // One poll now (what the timer does). force = re-check even if the files look unchanged.
    void poll (bool force = false);

private:
    void timerCallback() override { poll(); }
    juce::String stamp() const;

    const Licence& licence;
    std::function<void (const LicenceInfo&)> onChange;
    LicenceInfo last;
    juce::String files;
    juce::uint32 lastFull = 0;
    int lastDays = 0;
    const int fullMs;
};
} // namespace hz
