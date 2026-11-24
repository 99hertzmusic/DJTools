#include "Licence.h"

extern "C"
{
#include "tweetnacl.h"
    // TweetNaCl wants this for key generation, which plugins never do (we only verify).
    void randombytes (unsigned char* p, unsigned long long n) { for (unsigned long long i = 0; i < n; ++i) p[i] = 0; }
}

namespace hz
{
// Public half of the 99Hertz licence signing key (the secret half never ships).
static const char* publicKeyHex = "047cfd66e3c22a0350631b2884d09092ce18efe8ea1c4870269e90cbd1f18fbe";

static juce::MemoryBlock fromBase64Url (juce::String s)
{
    s = s.replaceCharacter ('-', '+').replaceCharacter ('_', '/');
    while (s.length() % 4 != 0) s << "=";
    juce::MemoryOutputStream out;
    if (! juce::Base64::convertFromBase64 (out, s)) return {};
    return out.getMemoryBlock();
}

int LicenceInfo::daysLeft() const
{
    if (expires == 0) return 9999;
    // 5 minutes' grace, so a trial issued a moment ago shows 7 days, not 8.
    const auto secs = expires - juce::Time::currentTimeMillis() / 1000 - 300;
    return secs <= 0 ? 0 : (int) ((secs + 86399) / 86400);
}

juce::String LicenceInfo::daysLeftText() const
{
    const auto d = daysLeft();
    return juce::String (d) + (d == 1 ? " DAY LEFT" : " DAYS LEFT");
}

juce::String LicenceInfo::buttonText() const
{
    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    switch (kind)
    {
        case pro:          return "LICENSED";
        case trial:        return "TRIAL" + dot + daysLeftText() + dot + "UPGRADE";
        case trialExpired: return "TRIAL ENDED" + dot + "UPGRADE";
        default:           return "UPGRADE";
    }
}

juce::String Licence::cleanKey (const juce::String& pasted)
{
    auto s = pasted;
    const auto start = s.indexOf ("99HZ-");
    if (start > 0) s = s.substring (start);
    s = s.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.");
    while (s.endsWithChar ('.')) s = s.dropLastCharacters (1);
    return s;
}

LicenceInfo Licence::verify (const juce::String& rawKey, const juce::String& productId)
{
    LicenceInfo info;
    auto key = cleanKey (rawKey);
    if (key.isEmpty()) return info;
    info.kind = LicenceInfo::invalid;
    if (! key.startsWith ("99HZ-")) return info;
    key = key.substring (5);
    const auto dot = key.indexOfChar ('.');
    if (dot <= 0) return info;

    const auto payload = fromBase64Url (key.substring (0, dot));
    const auto sig     = fromBase64Url (key.substring (dot + 1));
    if (payload.getSize() == 0 || sig.getSize() != 64) return info;

    juce::MemoryBlock pk;
    pk.loadFromHexString (publicKeyHex);
    if (pk.getSize() != 32) return info;

    // crypto_sign_open wants signature || message
    std::vector<unsigned char> sm (64 + payload.getSize()), m (sm.size());
    std::memcpy (sm.data(), sig.getData(), 64);
    std::memcpy (sm.data() + 64, payload.getData(), payload.getSize());
    unsigned long long mlen = 0;
    if (crypto_sign_open (m.data(), &mlen, sm.data(), (unsigned long long) sm.size(), (const unsigned char*) pk.getData()) != 0)
        return info;

    const auto json = juce::JSON::parse (payload.toString());
    const auto p = json["p"].toString();
    if (p != productId && p != "all") return info;

    info.product = p;
    info.email   = json["e"].toString();
    info.expires = (juce::int64) json["x"];
    const bool isTrial = json["t"].toString() == "trial";
    if (! isTrial) info.kind = LicenceInfo::pro;
    else           info.kind = info.daysLeft() > 0 ? LicenceInfo::trial : LicenceInfo::trialExpired;
    return info;
}

juce::File Licence::keyFile() const
{
   #if JUCE_MAC
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support");
   #else
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #endif
    return dir.getChildFile ("99Hertz").getChildFile ("Licences").getChildFile (product + ".key");
}

LicenceInfo Licence::check() const
{
    const auto f = keyFile();
    if (! f.existsAsFile()) return {};
    return verify (f.loadFileAsString(), product);
}

LicenceInfo Licence::apply (const juce::String& key) const
{
    auto info = verify (key, product);
    if (info.kind == LicenceInfo::invalid || info.kind == LicenceInfo::none) return info;

    // Never let a locked key (an ended trial) replace an unlocked one, or a trial replace pro.
    const auto current = check();
    if (current.unlocked() && ! info.unlocked()) return current;
    if (current.kind == LicenceInfo::pro && info.kind != LicenceInfo::pro) return current;

    const auto f = keyFile();
    const auto clean = cleanKey (key);
    f.getParentDirectory().createDirectory();
    if (! f.replaceWithText (clean)) { info.kind = LicenceInfo::saveFailed; return info; }

    // Read it back: if the folder silently refused the write, say so rather than unlock until restart.
    const auto saved = check();
    if (! saved.sameAs (info)) { info.kind = LicenceInfo::saveFailed; return info; }
    return info;
}

void Licence::requestTrial (const juce::String& email, bool subscribe,
                            std::function<void (LicenceInfo, juce::String)> done) const
{
    const auto prod = product;
    const auto self = *this;
    juce::Thread::launch ([prod, self, email, subscribe, done]
    {
        juce::DynamicObject::Ptr body (new juce::DynamicObject());
        body->setProperty ("email", email.trim());
        body->setProperty ("product", prod);
        body->setProperty ("subscribe", subscribe);
       #if JUCE_MAC
        body->setProperty ("os", "mac");
       #else
        body->setProperty ("os", "windows");
       #endif

        juce::String error;
        LicenceInfo info;
        const auto url = juce::URL (trialUrl).withPOSTData (juce::JSON::toString (juce::var (body.get())));
        int status = 0;
        auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                                 .withExtraHeaders ("Content-Type: application/json")
                                                 .withConnectionTimeoutMs (12000)
                                                 .withStatusCode (&status));
        if (stream == nullptr)
            error = "Couldn't reach 99hertz.com. Check your internet and try again.";
        else
        {
            const auto reply = juce::JSON::parse (stream->readEntireStreamAsString());
            if (! reply.isObject())
                error = "Couldn't start the trial. Try again or email info@99hertz.com.";   // e.g. an HTML error page
            else if ((bool) reply["ok"])
            {
                info = self.apply (reply["key"].toString());
                if (info.kind == LicenceInfo::saveFailed)
                    error = "Couldn't save your key. Email info@99hertz.com.";
                else if (! info.unlocked())
                    error = info.kind == LicenceInfo::trialExpired ? "Your free trial for this email has already ended."
                                                                   : "Something went wrong with the trial key. Email info@99hertz.com.";
            }
            else
                error = reply["error"].toString().isNotEmpty() ? reply["error"].toString()
                                                               : "Couldn't start the trial. Try again or email info@99hertz.com.";
        }
        juce::MessageManager::callAsync ([done, info, error] { done (info, error); });
    });
}

// ── LicenceWatcher ────────────────────────────────────────────────────────────
LicenceWatcher::LicenceWatcher (const Licence& l, LicenceInfo current, std::function<void (const LicenceInfo&)> cb,
                                int pollMs, int fullCheckMs)
    : licence (l), onChange (std::move (cb)), last (std::move (current)), fullMs (fullCheckMs)
{
    lastDays = last.daysLeft();
    stamp();
    lastFull = juce::Time::getMillisecondCounter();
    startTimer (pollMs);
}

LicenceWatcher::~LicenceWatcher() { stopTimer(); }

void LicenceWatcher::stamp()
{
    const auto f = licence.keyFile();
    fileSize = f.existsAsFile() ? f.getSize() : -1;
    fileTime = fileSize >= 0 ? f.getLastModificationTime().toMilliseconds() : 0;
}

void LicenceWatcher::poll (bool force)
{
    const auto oldTime = fileTime, oldSize = fileSize;
    stamp();
    const auto now = juce::Time::getMillisecondCounter();
    const bool fileChanged = fileTime != oldTime || fileSize != oldSize;
    const bool due = now - lastFull >= (juce::uint32) fullMs;
    if (! (force || fileChanged || due)) return;
    lastFull = now;

    const auto info = licence.check();
    const auto days = info.daysLeft();
    if (info.sameAs (last) && days == lastDays) return;
    last = info; lastDays = days;
    if (onChange) onChange (info);
}
} // namespace hz
