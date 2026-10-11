#include "prompt/prompts/opportunities_prompt.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

namespace {

// opportunities.sh is standalone (no _common.sh): it never reads stdin, never
// embeds files, and applies no --head. --help/-h anywhere and the empty
// category both print the help text; the native dispatcher intercepts
// --help/-h before we run, so execute only sees the no-category case.
std::string opportunities_help() noexcept {
    return "Usage: prompt opportunities [CATEGORY] [EXTRA...]\n"
           "       prompt opportunities money\n"
           "       prompt opportunities cpp \"backend development\"\n"
           "       prompt opportunities open-source \"compilers\"\n"
           "\n"
           "Find opportunities in a specific category such as money, tech, cpp, career, or business.\n"
           "Use the category `all` for a brief overview of every category.\n"
           "\n"
           "Categories:\n"
           "  money        Find ways to make money (freelancing, products, services)\n"
           "  tech         Technology trends and opportunities\n"
           "  cpp          C++ ecosystem opportunities (libraries, tools, jobs)\n"
           "  open-source  Open source projects to contribute to or start\n"
           "  career       Career growth and job opportunities\n"
           "  business     Business and startup ideas\n"
           "  invest       Investment and financial opportunities\n"
           "  learn        Learning paths and skill development\n"
           "  ai           AI and machine learning opportunities\n"
           "  remote       Remote work opportunities\n"
           "  freelance    Freelancing gigs, platforms, and niches\n"
           "  side-project Side project ideas with monetization potential\n"
           "  content      Content creation (YouTube, blog, podcast, social)\n"
           "  security     Cybersecurity opportunities\n"
           "  cloud        Cloud, DevOps, and infrastructure opportunities\n"
           "  data         Data science, analytics, and engineering\n"
           "  automation   Automation and productivity tooling\n"
           "  health       Health tech and wellness opportunities\n"
           "  creative     Creative, design, and artistic opportunities\n"
           "  all          Show a brief overview of all categories\n"
           "\n"
           "Options:\n"
           "  --help, -h   Show this help message\n";
}

// Append the user-extra line exactly as the script's `if [[ -n "$extra" ]]`.
void append_extra(std::string& out, std::string_view extra) noexcept {
    if (!extra.empty()) out += "Additional context from user: " + std::string(extra) + "\n";
}

bool in(std::string const& cat, std::initializer_list<char const*> names) noexcept {
    for (auto n : names)
        if (cat == n) return true;
    return false;
}

} // namespace

prompt_result execute_opportunities(prompt_context&& ctx) noexcept {
    // CATEGORY="${1:-}"; shift; EXTRA="$*"
    if (ctx.args_count == 0 || ctx.args[0].empty()) {
        return {opportunities_help(), 0, false, std::string{}};
    }

    std::string category(ctx.args[0]);
    std::string extra;
    for (std::size_t i = 1; i < ctx.args_count; ++i) {
        if (!extra.empty()) extra += ' ';
        extra += std::string(ctx.args[i]);
    }

    std::string out;

    if (in(category, {"money", "earn"})) {
        out += "You are an opportunity researcher focused on making money.\n"
               "Given the context below (if any), find concrete, actionable opportunities to earn money.\n"
               "\n"
               "For each opportunity, provide:\n"
               "- **What** it is (specific product, service, freelance niche, or side project)\n"
               "- **Why now** (why this is timely or underserved)\n"
               "- **Effort level** (low / medium / high)\n"
               "- **Time to first dollar** (estimate)\n"
               "- **How to start** (first 3 concrete steps)\n"
               "\n"
               "Focus on realistic, practical ideas. Avoid generic advice like 'learn to code'.\n"
               "Prefer 5-8 strong ideas over a long list of weak ones.\n";
        append_extra(out, extra);
    } else if (in(category, {"tech", "technology"})) {
        out += "You are a technology analyst identifying emerging opportunities.\n"
               "Given the context below (if any), find technology trends and opportunities worth pursuing.\n"
               "\n"
               "For each opportunity, provide:\n"
               "- **Area** (specific technology or domain)\n"
               "- **Opportunity** (what can be built, sold, or contributed to)\n"
               "- **Market signal** (evidence this is growing or underserved)\n"
               "- **Entry point** (how someone with moderate skill can get involved)\n"
               "- **Risk** (what could go wrong or what to watch for)\n"
               "\n"
               "Focus on 2025-2026 trends. Be specific \xE2\x80\x94 not just 'AI' but what within AI.\n";
        append_extra(out, extra);
    } else if (in(category, {"cpp", "c++"})) {
        out += "You are a C++ ecosystem opportunity finder.\n"
               "Given the context below (if any), find opportunities within the C++ world.\n"
               "\n"
               "Categories to explore:\n"
               "- **Open source** \xE2\x80\x94 libraries, tools, or frameworks the ecosystem needs\n"
               "- **Jobs** \xE2\x80\x94 roles, companies, or niches where C++ expertise is in demand\n"
               "- **Consulting** \xE2\x80\x94 areas where C++ specialists are\xE7\xA8\x80\xE7\xBC\xBA\n"
               "- **Education** \xE2\x80\x94 courses, books, or content the community lacks\n"
               "- **Tooling** \xE2\x80\x94 developer experience gaps in the C++ build/test/deploy chain\n"
               "\n"
               "For each, explain what exists today, what's missing, and how to fill the gap.\n";
        append_extra(out, extra);
    } else if (in(category, {"open-source", "oss"})) {
        out += "You are an open source opportunity analyst.\n"
               "Given the context below (if any), find open source project opportunities.\n"
               "\n"
               "For each opportunity, provide:\n"
               "- **Project idea** (what to build or contribute to)\n"
               "- **Gap** (what existing projects don't cover well)\n"
               "- **Tech stack** (languages, frameworks involved)\n"
               "- **Difficulty** (beginner / intermediate / advanced)\n"
               "- **Impact** (who benefits and how many)\n"
               "- **First step** (how to start this week)\n"
               "\n"
               "Consider both creating new projects and contributing to existing ones.\n"
               "Look for underserved niches, not saturated spaces.\n";
        append_extra(out, extra);
    } else if (in(category, {"career", "job"})) {
        out += "You are a career strategist identifying growth opportunities.\n"
               "Given the context below (if any), find career advancement opportunities.\n"
               "\n"
               "For each opportunity, provide:\n"
               "- **Path** (role, specialization, or transition)\n"
               "- **Why** (demand signal, salary data, or growth trajectory)\n"
               "- **Requirements** (skills needed, current gap)\n"
               "- **Timeline** (how long to prepare)\n"
               "- **First move** (actionable next step)\n"
               "\n"
               "Focus on high-signal, actionable paths. Avoid generic 'network more' advice.\n";
        append_extra(out, extra);
    } else if (in(category, {"business", "startup"})) {
        out += "You are a business opportunity researcher.\n"
               "Given the context below (if any), find business and startup opportunities.\n"
               "\n"
               "For each opportunity, provide:\n"
               "- **Idea** (what to build or sell)\n"
               "- **Target market** (who pays for this)\n"
               "- **Why now** (market timing or gap)\n"
               "- **Competitive landscape** (who else is doing this, how to differentiate)\n"
               "- **Revenue model** (how money flows)\n"
               "- **MVP scope** (smallest version that proves demand)\n"
               "\n"
               "Focus on bootstrappable ideas with clear paths to revenue.\n";
        append_extra(out, extra);
    } else if (in(category, {"invest", "investment"})) {
        out += "You are an investment opportunity analyst.\n"
               "Given the context below (if any), find investment opportunities.\n"
               "\n"
               "For each opportunity, provide:\n"
               "- **Asset class** (stocks, real estate, crypto, private equity, etc.)\n"
               "- **Thesis** (why this is compelling right now)\n"
               "- **Risk level** (conservative / moderate / aggressive)\n"
               "- **Time horizon** (short-term trade vs long-term hold)\n"
               "- **Research starting point** (where to learn more)\n"
               "\n"
               "Be specific and data-driven. Include caveats and risks.\n"
               "This is for informational purposes, not financial advice.\n";
        append_extra(out, extra);
    } else if (in(category, {"learn", "skill", "education"})) {
        out += "You are a learning opportunity advisor.\n"
               "Given the context below (if any), find high-value learning opportunities.\n"
               "\n"
               "For each, provide:\n"
               "- **Skill** (what to learn)\n"
               "- **Why valuable** (career impact, earning potential, or personal growth)\n"
               "- **Best resources** (specific courses, books, or projects)\n"
               "- **Time investment** (hours/days/weeks to reach useful proficiency)\n"
               "- **Practice project** (hands-on way to solidify the skill)\n"
               "\n"
               "Prioritize skills with the highest ROI for the user's apparent interests.\n";
        append_extra(out, extra);
    } else if (in(category, {"ai", "ml", "machine-learning"})) {
        out += "You are an AI/ML opportunity researcher.\n"
               "Given the context below (if any), find opportunities in artificial intelligence and machine learning.\n"
               "\n"
               "For each opportunity, provide:\n"
               "- **Domain** (specific AI/ML area: LLMs, computer vision, NLP, agents, etc.)\n"
               "- **Opportunity** (what to build, sell, or research)\n"
               "- **Barrier to entry** (compute cost, data access, expertise needed)\n"
               "- **Differentiation** (why your angle is not the obvious one everyone is doing)\n"
               "- **Monetization** (how to make money or gain influence)\n"
               "- **First step** (actionable starting point)\n"
               "\n"
               "Avoid hype cycles. Focus on durable opportunities with real demand.\n";
        append_extra(out, extra);
    } else if (in(category, {"remote", "wfh", "work-from-home"})) {
        out += "You are a remote work opportunity analyst.\n"
               "Given the context below (if any), find remote work opportunities.\n"
               "\n"
               "For each opportunity, provide:\n"
               "- **Role or gig** (specific job title or task)\n"
               "- **Where to find it** (platforms, job boards, communities)\n"
               "- **Pay range** (realistic estimates)\n"
               "- **Requirements** (skills, timezone, equipment)\n"
               "- **Competitiveness** (how crowded this space is)\n"
               "\n"
               "Focus on legitimate, sustainable remote income. Avoid MLMs or 'passive income' scams.\n";
        append_extra(out, extra);
    } else if (in(category, {"freelance", "gig"})) {
        out += "You are a freelancing opportunity strategist.\n"
               "Given the context below (if any), find freelancing and gig economy opportunities.\n"
               "\n"
               "For each opportunity, provide:\n"
               "- **Niche** (specific service or skill)\n"
               "- **Target client** (who pays for this)\n"
               "- **Pricing** (hourly, project, or retainer range)\n"
               "- **Platform** (where to find clients: Upwork, Fiverr, direct outreach, etc.)\n"
               "- **Differentiation** (how to stand out from other freelancers)\n"
               "- **Portfolio starter** (what to show even with no prior work)\n"
               "\n"
               "Prefer niches with recurring revenue over one-off gigs.\n";
        append_extra(out, extra);
    } else if (in(category, {"side-project", "sideproject", "side"})) {
        out += "You are a side project opportunity advisor.\n"
               "Given the context below (if any), find side project ideas.\n"
               "\n"
               "For each idea, provide:\n"
               "- **Concept** (one-line description)\n"
               "- **Why people would use it** (the pain point)\n"
               "- **Build time** (days/weeks to MVP)\n"
               "- **Monetization** (freemium, ads, subscription, one-time)\n"
               "- **Tech stack** (suggested stack or existing tools to build on)\n"
               "- **Audience** (where to find first 100 users)\n"
               "\n"
               "Focus on projects that are small enough to finish but useful enough to sell.\n";
        append_extra(out, extra);
    } else if (in(category, {"content", "media", "creator"})) {
        out += "You are a content creation opportunity analyst.\n"
               "Given the context below (if any), find content creation opportunities.\n"
               "\n"
               "For each opportunity, provide:\n"
               "- **Format** (YouTube, blog, podcast, newsletter, social media)\n"
               "- **Topic/niche** (specific subject area)\n"
               "- **Audience** (who would watch/read/listen)\n"
               "- **Gap** (what existing creators aren't covering well)\n"
               "- **Monetization** (ads, sponsors, products, courses)\n"
               "- **First piece** (exact topic for your first piece of content)\n"
               "\n"
               "Focus on niches where you have genuine expertise or unique perspective.\n";
        append_extra(out, extra);
    } else if (in(category, {"security", "cybersecurity", "infosec"})) {
        out += "You are a cybersecurity opportunity researcher.\n"
               "Given the context below (if any), find opportunities in cybersecurity.\n"
               "\n"
               "For each opportunity, provide:\n"
               "- **Area** (offensive, defensive, compliance, tooling, etc.)\n"
               "- **Opportunity** (what to build, audit, or specialize in)\n"
               "- **Demand signal** (regulations, threat landscape, talent shortage)\n"
               "- **Entry path** (certifications, labs, or projects to start)\n"
               "- **Income potential** (salary or contract range)\n"
               "\n"
               "Focus on practical, in-demand skills. Not just 'get a CISSP'.\n";
        append_extra(out, extra);
    } else if (in(category, {"cloud", "devops", "infra", "infrastructure"})) {
        out += "You are a cloud and DevOps opportunity analyst.\n"
               "Given the context below (if any), find cloud/DevOps/infrastructure opportunities.\n"
               "\n"
               "For each opportunity, provide:\n"
               "- **Domain** (AWS/GCP/Azure, Kubernetes, CI/CD, IaC, SRE, etc.)\n"
               "- **Opportunity** (what to build, automate, or consult on)\n"
               "- **Why now** (migration waves, tooling gaps, talent shortage)\n"
               "- **Certification or skill** (most valuable credential)\n"
               "- **Side income** (templates, tools, or services to sell)\n"
               "\n"
               "Focus on areas where companies are actively spending money right now.\n";
        append_extra(out, extra);
    } else if (in(category, {"data", "analytics", "data-science"})) {
        out += "You are a data opportunity researcher.\n"
               "Given the context below (if any), find data science, analytics, and data engineering opportunities.\n"
               "\n"
               "For each opportunity, provide:\n"
               "- **Area** (analytics, ML pipelines, data engineering, visualization, etc.)\n"
               "- **Opportunity** (what to build, analyze, or sell)\n"
               "- **Tools** (specific technologies involved)\n"
               "- **Industry** (which sector needs this most)\n"
               "- **Monetization** (employment, consulting, SaaS, datasets)\n"
               "\n"
               "Be specific. 'Learn Python' is not an opportunity. 'Build a churn prediction tool for SaaS' is.\n";
        append_extra(out, extra);
    } else if (in(category, {"automation", "productivity", "tools"})) {
        out += "You are an automation opportunity finder.\n"
               "Given the context below (if any), find automation and productivity tooling opportunities.\n"
               "\n"
               "For each opportunity, provide:\n"
               "- **Pain point** (what repetitive task people hate)\n"
               "- **Solution** (what to automate and how)\n"
               "- **Target user** (developers, businesses, specific role)\n"
               "- **Build complexity** (simple script / CLI tool / SaaS)\n"
               "- **Revenue potential** (free, open source goodwill, or paid)\n"
               "- **Integration** (what existing tools it connects to)\n"
               "\n"
               "Focus on automations that save real time for real people.\n";
        append_extra(out, extra);
    } else if (in(category, {"health", "wellness", "medtech"})) {
        out += "You are a health tech opportunity researcher.\n"
               "Given the context below (if any), find health and wellness tech opportunities.\n"
               "\n"
               "For each opportunity, provide:\n"
               "- **Area** (fitness, mental health, clinical, wearables, telehealth, etc.)\n"
               "- **Opportunity** (app, device, service, or research gap)\n"
               "- **Regulatory note** (FDA, HIPAA, or other compliance considerations)\n"
               "- **Audience** (patients, providers, insurers)\n"
               "- **Monetization** (B2B, B2C, insurance reimbursement)\n"
               "\n"
               "Be mindful of health claims. Focus on tools, not medical advice.\n";
        append_extra(out, extra);
    } else if (in(category, {"creative", "design", "art"})) {
        out += "You are a creative opportunity strategist.\n"
               "Given the context below (if any), find creative, design, and artistic opportunities.\n"
               "\n"
               "For each opportunity, provide:\n"
               "- **Medium** (graphic design, illustration, motion, 3D, photography, music, etc.)\n"
               "- **Opportunity** (freelance niche, product, template, or asset)\n"
               "- **Marketplace** (where to sell or showcase: Etsy, Gumroad, Dribbble, etc.)\n"
               "- **AI angle** (how AI tools enhance rather than replace the work)\n"
               "- **Pricing** (what the market bears)\n"
               "\n"
               "Focus on opportunities where taste and skill still matter, not where AI makes it trivial.\n";
        append_extra(out, extra);
    } else if (category == "all") {
        out += "You are an opportunity researcher. Provide a brief overview of opportunities across all domains.\n"
               "\n"
               "Give 2-3 high-signal opportunities for each of these categories:\n"
               "1. **Money** \xE2\x80\x94 Ways to earn\n"
               "2. **Technology** \xE2\x80\x94 Tech trends to ride\n"
               "3. **C++** \xE2\x80\x94 C++ ecosystem gaps\n"
               "4. **Open Source** \xE2\x80\x94 Projects to start or join\n"
               "5. **Career** \xE2\x80\x94 Growth paths\n"
               "6. **Business** \xE2\x80\x94 Bootstrappable ideas\n"
               "7. **Investing** \xE2\x80\x94 Asset opportunities\n"
               "8. **Learning** \xE2\x80\x94 High-ROI skills\n"
               "9. **AI/ML** \xE2\x80\x94 Artificial intelligence angles\n"
               "10. **Remote Work** \xE2\x80\x94 Work-from-home gigs\n"
               "11. **Freelancing** \xE2\x80\x94 Client work niches\n"
               "12. **Side Projects** \xE2\x80\x94 Build-and-sell ideas\n"
               "13. **Content** \xE2\x80\x94 Creator economy opportunities\n"
               "14. **Security** \xE2\x80\x94 Cybersecurity demand areas\n"
               "15. **Cloud/DevOps** \xE2\x80\x94 Infrastructure opportunities\n"
               "16. **Data** \xE2\x80\x94 Analytics and data engineering\n"
               "17. **Automation** \xE2\x80\x94 Productivity tooling gaps\n"
               "18. **Health Tech** \xE2\x80\x94 Wellness and medtech spaces\n"
               "19. **Creative** \xE2\x80\x94 Design and artistic niches\n"
               "\n"
               "Keep each entry concise \xE2\x80\x94 one line per idea with a brief 'why now'.\n";
        append_extra(out, extra);
    } else {
        std::string err =
            "Error: Unknown category '" + category +
            "'\n"
            "Available categories: money, tech, cpp, open-source, career, business, invest, learn, ai, "
            "remote, freelance, side-project, content, security, cloud, data, automation, health, creative, all\n";
        return {std::string{}, 1, false, std::move(err)};
    }

    return {std::move(out), 0, false, std::string{}};
}

void render_help_opportunities(std::ostream& os) noexcept { os << opportunities_help(); }

} // namespace prompt::prompts
