#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt opportunities [CATEGORY] [EXTRA...]
       prompt opportunities money
       prompt opportunities cpp "backend development"
       prompt opportunities open-source "compilers"

Find opportunities in a specific category. Categories:
  money        Find ways to make money (freelancing, products, services)
  tech         Technology trends and opportunities
  cpp          C++ ecosystem opportunities (libraries, tools, jobs)
  open-source  Open source projects to contribute to or start
  career       Career growth and job opportunities
  business     Business and startup ideas
  invest       Investment and financial opportunities
  learn        Learning paths and skill development
  ai           AI and machine learning opportunities
  remote       Remote work opportunities
  freelance    Freelancing gigs, platforms, and niches
  side-project Side project ideas with monetization potential
  content      Content creation (YouTube, blog, podcast, social)
  security     Cybersecurity opportunities
  cloud        Cloud, DevOps, and infrastructure opportunities
  data         Data science, analytics, and engineering
  automation   Automation and productivity tooling
  health       Health tech and wellness opportunities
  creative     Creative, design, and artistic opportunities
  all          Show a brief overview of all categories

Options:
  --help, -h   Show this help message
EOF
}

for arg in "$@"; do
  if [[ "$arg" == "--help" || "$arg" == "-h" ]]; then
    show_help
    exit 0
  fi
done

CATEGORY="${1:-}"
shift 2>/dev/null || true
EXTRA="$*"

if [[ -z "$CATEGORY" ]]; then
  show_help
  exit 0
fi

show_category_prompt() {
  local cat="$1"
  local extra="${2:-}"

  case "$cat" in
    money|earn)
      echo "You are an opportunity researcher focused on making money."
      echo "Given the context below (if any), find concrete, actionable opportunities to earn money."
      echo
      echo "For each opportunity, provide:"
      echo "- **What** it is (specific product, service, freelance niche, or side project)"
      echo "- **Why now** (why this is timely or underserved)"
      echo "- **Effort level** (low / medium / high)"
      echo "- **Time to first dollar** (estimate)"
      echo "- **How to start** (first 3 concrete steps)"
      echo
      echo "Focus on realistic, practical ideas. Avoid generic advice like 'learn to code'."
      echo "Prefer 5-8 strong ideas over a long list of weak ones."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    tech|technology)
      echo "You are a technology analyst identifying emerging opportunities."
      echo "Given the context below (if any), find technology trends and opportunities worth pursuing."
      echo
      echo "For each opportunity, provide:"
      echo "- **Area** (specific technology or domain)"
      echo "- **Opportunity** (what can be built, sold, or contributed to)"
      echo "- **Market signal** (evidence this is growing or underserved)"
      echo "- **Entry point** (how someone with moderate skill can get involved)"
      echo "- **Risk** (what could go wrong or what to watch for)"
      echo
      echo "Focus on 2025-2026 trends. Be specific — not just 'AI' but what within AI."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    cpp|c++)
      echo "You are a C++ ecosystem opportunity finder."
      echo "Given the context below (if any), find opportunities within the C++ world."
      echo
      echo "Categories to explore:"
      echo "- **Open source** — libraries, tools, or frameworks the ecosystem needs"
      echo "- **Jobs** — roles, companies, or niches where C++ expertise is in demand"
      echo "- **Consulting** — areas where C++ specialists are稀缺"
      echo "- **Education** — courses, books, or content the community lacks"
      echo "- **Tooling** — developer experience gaps in the C++ build/test/deploy chain"
      echo
      echo "For each, explain what exists today, what's missing, and how to fill the gap."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    open-source|oss)
      echo "You are an open source opportunity analyst."
      echo "Given the context below (if any), find open source project opportunities."
      echo
      echo "For each opportunity, provide:"
      echo "- **Project idea** (what to build or contribute to)"
      echo "- **Gap** (what existing projects don't cover well)"
      echo "- **Tech stack** (languages, frameworks involved)"
      echo "- **Difficulty** (beginner / intermediate / advanced)"
      echo "- **Impact** (who benefits and how many)"
      echo "- **First step** (how to start this week)"
      echo
      echo "Consider both creating new projects and contributing to existing ones."
      echo "Look for underserved niches, not saturated spaces."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    career|job)
      echo "You are a career strategist identifying growth opportunities."
      echo "Given the context below (if any), find career advancement opportunities."
      echo
      echo "For each opportunity, provide:"
      echo "- **Path** (role, specialization, or transition)"
      echo "- **Why** (demand signal, salary data, or growth trajectory)"
      echo "- **Requirements** (skills needed, current gap)"
      echo "- **Timeline** (how long to prepare)"
      echo "- **First move** (actionable next step)"
      echo
      echo "Focus on high-signal, actionable paths. Avoid generic 'network more' advice."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    business|startup)
      echo "You are a business opportunity researcher."
      echo "Given the context below (if any), find business and startup opportunities."
      echo
      echo "For each opportunity, provide:"
      echo "- **Idea** (what to build or sell)"
      echo "- **Target market** (who pays for this)"
      echo "- **Why now** (market timing or gap)"
      echo "- **Competitive landscape** (who else is doing this, how to differentiate)"
      echo "- **Revenue model** (how money flows)"
      echo "- **MVP scope** (smallest version that proves demand)"
      echo
      echo "Focus on bootstrappable ideas with clear paths to revenue."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    invest|investment)
      echo "You are an investment opportunity analyst."
      echo "Given the context below (if any), find investment opportunities."
      echo
      echo "For each opportunity, provide:"
      echo "- **Asset class** (stocks, real estate, crypto, private equity, etc.)"
      echo "- **Thesis** (why this is compelling right now)"
      echo "- **Risk level** (conservative / moderate / aggressive)"
      echo "- **Time horizon** (short-term trade vs long-term hold)"
      echo "- **Research starting point** (where to learn more)"
      echo
      echo "Be specific and data-driven. Include caveats and risks."
      echo "This is for informational purposes, not financial advice."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    learn|skill|education)
      echo "You are a learning opportunity advisor."
      echo "Given the context below (if any), find high-value learning opportunities."
      echo
      echo "For each, provide:"
      echo "- **Skill** (what to learn)"
      echo "- **Why valuable** (career impact, earning potential, or personal growth)"
      echo "- **Best resources** (specific courses, books, or projects)"
      echo "- **Time investment** (hours/days/weeks to reach useful proficiency)"
      echo "- **Practice project** (hands-on way to solidify the skill)"
      echo
      echo "Prioritize skills with the highest ROI for the user's apparent interests."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    ai|ml|machine-learning)
      echo "You are an AI/ML opportunity researcher."
      echo "Given the context below (if any), find opportunities in artificial intelligence and machine learning."
      echo
      echo "For each opportunity, provide:"
      echo "- **Domain** (specific AI/ML area: LLMs, computer vision, NLP, agents, etc.)"
      echo "- **Opportunity** (what to build, sell, or research)"
      echo "- **Barrier to entry** (compute cost, data access, expertise needed)"
      echo "- **Differentiation** (why your angle is not the obvious one everyone is doing)"
      echo "- **Monetization** (how to make money or gain influence)"
      echo "- **First step** (actionable starting point)"
      echo
      echo "Avoid hype cycles. Focus on durable opportunities with real demand."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    remote|wfh|work-from-home)
      echo "You are a remote work opportunity analyst."
      echo "Given the context below (if any), find remote work opportunities."
      echo
      echo "For each opportunity, provide:"
      echo "- **Role or gig** (specific job title or task)"
      echo "- **Where to find it** (platforms, job boards, communities)"
      echo "- **Pay range** (realistic estimates)"
      echo "- **Requirements** (skills, timezone, equipment)"
      echo "- **Competitiveness** (how crowded this space is)"
      echo
      echo "Focus on legitimate, sustainable remote income. Avoid MLMs or 'passive income' scams."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    freelance|gig)
      echo "You are a freelancing opportunity strategist."
      echo "Given the context below (if any), find freelancing and gig economy opportunities."
      echo
      echo "For each opportunity, provide:"
      echo "- **Niche** (specific service or skill)"
      echo "- **Target client** (who pays for this)"
      echo "- **Pricing** (hourly, project, or retainer range)"
      echo "- **Platform** (where to find clients: Upwork, Fiverr, direct outreach, etc.)"
      echo "- **Differentiation** (how to stand out from other freelancers)"
      echo "- **Portfolio starter** (what to show even with no prior work)"
      echo
      echo "Prefer niches with recurring revenue over one-off gigs."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    side-project|sideproject|side)
      echo "You are a side project opportunity advisor."
      echo "Given the context below (if any), find side project ideas."
      echo
      echo "For each idea, provide:"
      echo "- **Concept** (one-line description)"
      echo "- **Why people would use it** (the pain point)"
      echo "- **Build time** (days/weeks to MVP)"
      echo "- **Monetization** (freemium, ads, subscription, one-time)"
      echo "- **Tech stack** (suggested stack or existing tools to build on)"
      echo "- **Audience** (where to find first 100 users)"
      echo
      echo "Focus on projects that are small enough to finish but useful enough to sell."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    content|media|creator)
      echo "You are a content creation opportunity analyst."
      echo "Given the context below (if any), find content creation opportunities."
      echo
      echo "For each opportunity, provide:"
      echo "- **Format** (YouTube, blog, podcast, newsletter, social media)"
      echo "- **Topic/niche** (specific subject area)"
      echo "- **Audience** (who would watch/read/listen)"
      echo "- **Gap** (what existing creators aren't covering well)"
      echo "- **Monetization** (ads, sponsors, products, courses)"
      echo "- **First piece** (exact topic for your first piece of content)"
      echo
      echo "Focus on niches where you have genuine expertise or unique perspective."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    security|cybersecurity|infosec)
      echo "You are a cybersecurity opportunity researcher."
      echo "Given the context below (if any), find opportunities in cybersecurity."
      echo
      echo "For each opportunity, provide:"
      echo "- **Area** (offensive, defensive, compliance, tooling, etc.)"
      echo "- **Opportunity** (what to build, audit, or specialize in)"
      echo "- **Demand signal** (regulations, threat landscape, talent shortage)"
      echo "- **Entry path** (certifications, labs, or projects to start)"
      echo "- **Income potential** (salary or contract range)"
      echo
      echo "Focus on practical, in-demand skills. Not just 'get a CISSP'."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    cloud|devops|infra|infrastructure)
      echo "You are a cloud and DevOps opportunity analyst."
      echo "Given the context below (if any), find cloud/DevOps/infrastructure opportunities."
      echo
      echo "For each opportunity, provide:"
      echo "- **Domain** (AWS/GCP/Azure, Kubernetes, CI/CD, IaC, SRE, etc.)"
      echo "- **Opportunity** (what to build, automate, or consult on)"
      echo "- **Why now** (migration waves, tooling gaps, talent shortage)"
      echo "- **Certification or skill** (most valuable credential)"
      echo "- **Side income** (templates, tools, or services to sell)"
      echo
      echo "Focus on areas where companies are actively spending money right now."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    data|analytics|data-science)
      echo "You are a data opportunity researcher."
      echo "Given the context below (if any), find data science, analytics, and data engineering opportunities."
      echo
      echo "For each opportunity, provide:"
      echo "- **Area** (analytics, ML pipelines, data engineering, visualization, etc.)"
      echo "- **Opportunity** (what to build, analyze, or sell)"
      echo "- **Tools** (specific technologies involved)"
      echo "- **Industry** (which sector needs this most)"
      echo "- **Monetization** (employment, consulting, SaaS, datasets)"
      echo
      echo "Be specific. 'Learn Python' is not an opportunity. 'Build a churn prediction tool for SaaS' is."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    automation|productivity|tools)
      echo "You are an automation opportunity finder."
      echo "Given the context below (if any), find automation and productivity tooling opportunities."
      echo
      echo "For each opportunity, provide:"
      echo "- **Pain point** (what repetitive task people hate)"
      echo "- **Solution** (what to automate and how)"
      echo "- **Target user** (developers, businesses, specific role)"
      echo "- **Build complexity** (simple script / CLI tool / SaaS)"
      echo "- **Revenue potential** (free, open source goodwill, or paid)"
      echo "- **Integration** (what existing tools it connects to)"
      echo
      echo "Focus on automations that save real time for real people."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    health|wellness|medtech)
      echo "You are a health tech opportunity researcher."
      echo "Given the context below (if any), find health and wellness tech opportunities."
      echo
      echo "For each opportunity, provide:"
      echo "- **Area** (fitness, mental health, clinical, wearables, telehealth, etc.)"
      echo "- **Opportunity** (app, device, service, or research gap)"
      echo "- **Regulatory note** (FDA, HIPAA, or other compliance considerations)"
      echo "- **Audience** (patients, providers, insurers)"
      echo "- **Monetization** (B2B, B2C, insurance reimbursement)"
      echo
      echo "Be mindful of health claims. Focus on tools, not medical advice."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    creative|design|art)
      echo "You are a creative opportunity strategist."
      echo "Given the context below (if any), find creative, design, and artistic opportunities."
      echo
      echo "For each opportunity, provide:"
      echo "- **Medium** (graphic design, illustration, motion, 3D, photography, music, etc.)"
      echo "- **Opportunity** (freelance niche, product, template, or asset)"
      echo "- **Marketplace** (where to sell or showcase: Etsy, Gumroad, Dribbble, etc.)"
      echo "- **AI angle** (how AI tools enhance rather than replace the work)"
      echo "- **Pricing** (what the market bears)"
      echo
      echo "Focus on opportunities where taste and skill still matter, not where AI makes it trivial."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    all)
      echo "You are an opportunity researcher. Provide a brief overview of opportunities across all domains."
      echo
      echo "Give 2-3 high-signal opportunities for each of these categories:"
      echo "1. **Money** — Ways to earn"
      echo "2. **Technology** — Tech trends to ride"
      echo "3. **C++** — C++ ecosystem gaps"
      echo "4. **Open Source** — Projects to start or join"
      echo "5. **Career** — Growth paths"
      echo "6. **Business** — Bootstrappable ideas"
      echo "7. **Investing** — Asset opportunities"
      echo "8. **Learning** — High-ROI skills"
      echo "9. **AI/ML** — Artificial intelligence angles"
      echo "10. **Remote Work** — Work-from-home gigs"
      echo "11. **Freelancing** — Client work niches"
      echo "12. **Side Projects** — Build-and-sell ideas"
      echo "13. **Content** — Creator economy opportunities"
      echo "14. **Security** — Cybersecurity demand areas"
      echo "15. **Cloud/DevOps** — Infrastructure opportunities"
      echo "16. **Data** — Analytics and data engineering"
      echo "17. **Automation** — Productivity tooling gaps"
      echo "18. **Health Tech** — Wellness and medtech spaces"
      echo "19. **Creative** — Design and artistic niches"
      echo
      echo "Keep each entry concise — one line per idea with a brief 'why now'."
      if [[ -n "$extra" ]]; then
        echo "Additional context from user: $extra"
      fi
      ;;
    *)
      echo "Error: Unknown category '$cat'" >&2
      echo "Available categories: money, tech, cpp, open-source, career, business, invest, learn, ai, remote, freelance, side-project, content, security, cloud, data, automation, health, creative, all" >&2
      exit 1
      ;;
  esac
}

show_category_prompt "$CATEGORY" "$EXTRA"
