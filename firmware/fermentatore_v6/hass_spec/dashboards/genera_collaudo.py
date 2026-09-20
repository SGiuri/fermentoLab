"""Genera f1_v6_collaudo_dashboard.yaml (dashboard "F1 Collaudo").

Uso: python genera_collaudo.py
I test sono quelli del catalogo in appdaemon/collaudo_f1/collaudo_motore.py.
"""
import pathlib
import sys

QUI = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(QUI.parent / "appdaemon" / "collaudo_f1"))
import collaudo_motore as cm  # noqa: E402

GRUPPI = {"A": "Fase A — firmware attuale, automatici",
          "B": "Fase B — firmware 6.5.0, automatici",
          "C": "Manuali — serve la tua presenza"}

ICONA_ESITO = """{% set e = states('sensor.f1_collaudo_ID') %}
              {{ {'superato': 'mdi:check-circle', 'avviso': 'mdi:alert-circle', 'fallito': 'mdi:close-circle',
                  'in corso': 'mdi:progress-clock', 'in coda': 'mdi:clock-outline', 'interrotto': 'mdi:cancel',
                  'non applicabile': 'mdi:minus-circle-outline', 'non eseguito': 'mdi:circle-outline'}.get(e, 'mdi:circle-outline') }}"""
COLORE_ESITO = """{% set e = states('sensor.f1_collaudo_ID') %}
              {{ {'superato': 'green', 'avviso': 'amber', 'fallito': 'red', 'in corso': 'blue', 'in coda': 'light-blue',
                  'interrotto': 'grey', 'non applicabile': 'disabled'}.get(e, 'disabled') }}"""


def scheda_test(t):
    icona = ICONA_ESITO.replace("ID", t.id)
    colore = COLORE_ESITO.replace("ID", t.id)
    return f"""          - type: custom:mushroom-template-card
            entity: input_boolean.f1_collaudo_sel_{t.id}
            primary: "{t.nome}"
            secondary: >-
              {{{{ states('sensor.f1_collaudo_{t.id}') | capitalize }}}} · {t.durata} · fw {t.fw_min}{' · presenza richiesta' if t.presenza != 'nessuna' else ''}
            icon: >-
              {icona}
            icon_color: >-
              {colore}
            badge_icon: "{{{{ 'mdi:check-bold' if is_state('input_boolean.f1_collaudo_sel_{t.id}', 'on') else '' }}}}"
            badge_color: blue
            multiline_secondary: true
            tap_action:
              action: toggle
            hold_action:
              action: more-info
              entity: sensor.f1_collaudo_{t.id}
"""


def main():
    parti = []
    for g, titolo in GRUPPI.items():
        schede = "".join(scheda_test(t) for t in cm.CATALOGO if t.gruppo == g)
        parti.append(f"""      - type: vertical-stack
        cards:
          - type: markdown
            content: "#### {titolo}"
{schede}""")
    elenco_test = "\n".join(parti)
    ids = [t.id for t in cm.CATALOGO]
    yaml = TEMPLATE.replace("@@ELENCO_TEST@@", elenco_test).replace("@@IDS@@", str(ids))
    (QUI / "f1_v6_collaudo_dashboard.yaml").write_text(yaml, encoding="utf-8", newline="\n")
    print("scritto f1_v6_collaudo_dashboard.yaml")


TEMPLATE = r"""# =============================================================================
# Dashboard "F1 Collaudo" — GENERATA da genera_collaudo.py: non modificare a mano.
# Test reali di F1 senza birra, eseguiti dall'app AppDaemon collaudo_f1.
# =============================================================================
title: F1 Collaudo
views:
  - title: Collaudo
    path: collaudo
    icon: mdi:clipboard-check-multiple
    cards:

      # ── STATO DELLA SESSIONE ─────────────────────────────────────────────
      - type: custom:mushroom-template-card
        entity: sensor.f1_collaudo_sessione
        primary: >-
          {% set s = states('sensor.f1_collaudo_sessione') %}
          {% if s == 'in corso' %}Collaudo in corso: {{ state_attr('sensor.f1_collaudo_sessione', 'test') }}
          {% elif s == 'in chiusura' %}Collaudo in chiusura: ritorno al parcheggio
          {% elif s == 'terminato' %}Collaudo terminato
          {% elif s == 'interrotto' %}Collaudo interrotto
          {% elif s in ['unknown', 'unavailable'] %}App di collaudo non attiva
          {% else %}Nessun collaudo in corso{% endif %}
        secondary: >-
          {% set a = states.sensor.f1_collaudo_sessione.attributes if states.sensor.f1_collaudo_sessione else {} %}
          {% if states('sensor.f1_collaudo_sessione') in ['in corso', 'in chiusura'] %}{{ a.get('fase', '') }}
          · test {{ a.get('completati', 0) }} di {{ a.get('totale', 0) }}
          {%- if a.get('stima_fine') %} · fine stimata {{ a.stima_fine | timestamp_custom('%a %d/%m %H:%M') }}{% endif %}
          {%- elif a.get('fine') %}Concluso {{ a.fine | timestamp_custom('%d/%m %H:%M') }}: {{ a.get('motivo', '') }}
          {%- else %}Scegli i test qui sotto e avviali.{% endif %}
        icon: >-
          {{ {'in corso': 'mdi:progress-clock', 'in chiusura': 'mdi:progress-upload', 'terminato': 'mdi:flag-checkered',
              'interrotto': 'mdi:cancel'}.get(states('sensor.f1_collaudo_sessione'), 'mdi:clipboard-outline') }}
        icon_color: >-
          {{ {'in corso': 'blue', 'in chiusura': 'blue', 'terminato': 'green', 'interrotto': 'grey'}.get(states('sensor.f1_collaudo_sessione'), 'disabled') }}
        multiline_secondary: true
        tap_action:
          action: none

      - type: custom:mushroom-template-card
        entity: sensor.f1_collaudo_verdetto
        primary: "Verdetto: {{ state_attr('sensor.f1_collaudo_verdetto', 'testo') or 'nessun test eseguito' }}"
        secondary: >-
          {% set f = state_attr('sensor.f1_collaudo_verdetto', 'falliti') or [] %}
          {% set a = state_attr('sensor.f1_collaudo_verdetto', 'avvisi') or [] %}
          {% set m = state_attr('sensor.f1_collaudo_verdetto', 'mancanti') or [] %}
          {% if f %}Falliti: {{ f | join(', ') }}. {% endif %}{% if a %}Avvisi: {{ a | join(', ') }}. {% endif %}{% if m %}Da eseguire: {{ m | join(', ') }}.{% endif %}
        icon: mdi:beer
        icon_color: >-
          {{ {'pronto': 'green', 'non pronto': 'red', 'incompleto': 'amber'}.get(states('sensor.f1_collaudo_verdetto'), 'disabled') }}
        multiline_secondary: true
        tap_action:
          action: none

      # ── MODALITÀ ────────────────────────────────────────────────────────
      - type: conditional
        conditions:
          - condition: state
            entity: input_select.f1_v6_modalita
            state_not: Collaudo
        card:
          type: custom:mushroom-template-card
          primary: Entra in modalità Collaudo
          secondary: >-
            Ora F1 è in {{ states('input_select.f1_v6_modalita') }}. I test cambiano setpoint e parametri:
            solo con il fermentatore senza birra.
          icon: mdi:clipboard-check
          icon_color: purple
          multiline_secondary: true
          tap_action:
            action: perform-action
            perform_action: script.f1_collaudo_entra
            confirmation:
              text: >-
                Confermi che nel fermentatore F1 NON c'è birra? I test portano l'acqua da 2 a 17 °C,
                riavviano l'ESP32 e simulano guasti. Gli allarmi mandano email come in produzione.

      # ── COMANDI ─────────────────────────────────────────────────────────
      - type: conditional
        conditions:
          - condition: state
            entity: input_select.f1_v6_modalita
            state: Collaudo
          - condition: state
            entity: sensor.f1_collaudo_sessione
            state_not: in corso
          - condition: state
            entity: sensor.f1_collaudo_sessione
            state_not: in chiusura
        card:
          type: vertical-stack
          cards:
            - type: markdown
              content: |
                {% set h = states('sensor.f1_collaudo_selezione') | float(0) %}
                {% set p = state_attr('sensor.f1_collaudo_selezione', 'presenza') or [] %}
                {% set n = (state_attr('sensor.f1_collaudo_selezione', 'test') or []) | count %}
                ### Test selezionati: {{ n }}, circa {{ h | round(0) | int }} ore
                {% if p %}**Serve la tua presenza:**
                {% for x in p %}
                - {{ x }}
                {% endfor %}{% else %}Nessun intervento fisico richiesto.{% endif %}
                {% if h > 72 %}

                ⚠️ Più di 3 giorni: togli qualche test.{% endif %}

                Seleziona o deseleziona un test toccandone la scheda; tieni premuto per i dettagli.
            - type: grid
              columns: 4
              square: false
              cards:
                - type: button
                  name: Fase A
                  icon: mdi:numeric-1-box-multiple
                  tap_action:
                    action: perform-action
                    perform_action: script.f1_collaudo_seleziona
                    data:
                      gruppo: A
                - type: button
                  name: Fase B
                  icon: mdi:numeric-2-box-multiple
                  tap_action:
                    action: perform-action
                    perform_action: script.f1_collaudo_seleziona
                    data:
                      gruppo: B
                - type: button
                  name: Manuali
                  icon: mdi:hand-back-right
                  tap_action:
                    action: perform-action
                    perform_action: script.f1_collaudo_seleziona
                    data:
                      gruppo: C
                - type: button
                  name: Nessuno
                  icon: mdi:checkbox-blank-off-outline
                  tap_action:
                    action: perform-action
                    perform_action: script.f1_collaudo_seleziona
                    data:
                      gruppo: nessuno
            - type: horizontal-stack
              cards:
                - type: button
                  name: Avvia i test selezionati
                  icon: mdi:play-circle
                  icon_height: 48px
                  card_mod:
                    style: |
                      ha-card { background: #1b5e20; color: white; }
                  tap_action:
                    action: perform-action
                    perform_action: script.f1_collaudo_avvia
                    confirmation:
                      text: >-
                        Avviare i test selezionati? Il collaudo invia comandi a F1 (profili, riavvii) finché non
                        finisce o lo interrompi. Per i test manuali la dashboard ti dirà quando intervenire.
                - type: button
                  name: Torna in Produzione
                  icon: mdi:beer
                  icon_height: 48px
                  tap_action:
                    action: perform-action
                    perform_action: script.f1_collaudo_esci
                    confirmation:
                      text: Uscire dalla modalità Collaudo e tornare in Produzione?

      - type: conditional
        conditions:
          - condition: state
            entity: sensor.f1_collaudo_sessione
            state: in corso
        card:
          type: grid
          columns: 2
          square: false
          cards:
            - type: button
              name: Salta il test in corso
              icon: mdi:skip-next-circle
              icon_height: 40px
              tap_action:
                action: perform-action
                perform_action: script.f1_collaudo_salta
                confirmation:
                  text: Saltare il test in corso? Risulterà interrotto e si passa al successivo.
            - type: button
              name: Interrompi il collaudo
              icon: mdi:stop-circle
              icon_height: 40px
              card_mod:
                style: |
                  ha-card { background: #b71c1c; color: white; }
              tap_action:
                action: perform-action
                perform_action: script.f1_collaudo_interrompi
                confirmation:
                  text: >-
                    Interrompere il collaudo? F1 torna al controllo di produzione (on/off, isteresi 0,5 °C) e alla
                    temperatura di parcheggio, poi in modalità Produzione.

      # ── ELENCO DEI TEST ─────────────────────────────────────────────────
@@ELENCO_TEST@@
      # ── ANDAMENTO ───────────────────────────────────────────────────────
      - type: custom:apexcharts-card
        header:
          show: true
          title: Temperatura durante il collaudo — 3 giorni
          show_states: true
          colorize_states: true
        graph_span: 3d
        update_interval: 2min
        now:
          show: true
        apex_config:
          chart:
            height: 300
          legend:
            show: true
        yaxis:
          - id: temp
            decimals: 1
          - id: rele
            show: false
            min: 0
            max: 4
        series:
          - entity: sensor.f1_v6_temperatura
            name: Temperatura
            yaxis_id: temp
            color: "#e53935"
            stroke_width: 2
            group_by:
              func: avg
              duration: 5min
          - entity: sensor.f1_v6_setpoint
            name: Setpoint
            yaxis_id: temp
            color: "#1e88e5"
            curve: stepline
            stroke_width: 2
          - entity: binary_sensor.f1_v6_riscaldatore
            name: Caldo
            yaxis_id: rele
            type: area
            curve: stepline
            color: "#fb8c00"
            opacity: 0.25
            stroke_width: 0
            transform: "return x === 'on' ? 1 : 0;"
            show:
              in_header: false
          - entity: binary_sensor.f1_v6_refrigeratore
            name: Pompa
            yaxis_id: rele
            type: area
            curve: stepline
            color: "#29b6f6"
            opacity: 0.25
            stroke_width: 0
            transform: "return x === 'on' ? 1 : 0;"
            show:
              in_header: false

      # ── RISULTATI ───────────────────────────────────────────────────────
      - type: markdown
        title: Risultati
        content: |
          {% set simbolo = {'superato': '✅', 'avviso': '⚠️', 'fallito': '❌', 'interrotto': '⏹️', 'non applicabile': '➖'} %}
          {% set ns = namespace(n=0) %}
          {% for id in @@IDS@@ %}
          {% set e = states('sensor.f1_collaudo_' ~ id) %}
          {% if e in simbolo %}{% set ns.n = ns.n + 1 %}
          {% set a = states['sensor.f1_collaudo_' ~ id].attributes %}
          ### {{ simbolo[e] }} {{ a.nome }} — {{ e }}
          {% if a.inizio and a.fine %}{{ a.inizio | timestamp_custom('%d/%m %H:%M') }} → {{ a.fine | timestamp_custom('%d/%m %H:%M') }}{% endif %}
          {% if a.note %}*{{ a.note }}*{% endif %}
          {% if a.controlli %}
          | | Verifica | Misurato | Atteso |
          |---|---|---|---|
          {% for c in a.controlli %}| {{ '✅' if c.ok else ('❌' if c.critico else '⚠️') }} | {{ c.voce }} | {{ c.valore }} | {{ c.limite }} |
          {% endfor %}
          {% endif %}
          {% if a.misure %}{% for k, v in a.misure.items() %}{{ k }}: **{{ v }}** · {% endfor %}{% endif %}

          {% endif %}
          {% endfor %}
          {% if ns.n == 0 %}Nessun risultato ancora.{% endif %}

      - type: markdown
        title: Registro della sessione
        content: |
          {% for e in (state_attr('sensor.f1_collaudo_sessione', 'eventi') or []) | reverse %}
          {% set p = e.split('|', 1) %}- {{ p[0] | int | timestamp_custom('%d/%m %H:%M') }} — {{ p[1] }}
          {% endfor %}

  - title: Guida
    path: guida
    icon: mdi:book-open-variant
    cards:
      - type: markdown
        content: |
          ## Come funziona il collaudo

          I test girano da soli nell'app di collaudo (AppDaemon) e mandano a F1 gli stessi comandi di
          Home Assistant: profili, parametri, riavvio. Ogni test ha criteri di superamento misurati e
          registra i risultati qui e nel registro attività. Un riavvio di Home Assistant o dell'app non
          perde il collaudo: riprende dal punto in cui era.

          **Durante il collaudo** F1 è in modalità *Collaudo*: gli script didattici sono bloccati e gli
          allarmi mandano le email come in produzione (il test dell'allarme ne manda una apposta). A fine
          collaudo, o se lo interrompi, F1 torna al controllo di produzione (on/off, isteresi 0,5 °C), alla
          temperatura di parcheggio ({{ states('input_number.f1_collaudo_temp_parcheggio') }} °C) e in
          modalità Produzione, e ricevi un'email con l'esito.

          **Limiti di sicurezza del collaudo:** se l'acqua scende sotto 0,5 °C o sale sopra 26 °C il collaudo
          si ferma. Restano sempre attive le sicurezze del firmware.

          ## Piano verso il 3 ottobre

          | Quando | Cosa | Presenza |
          |---|---|---|
          | Subito | **Fase A** (firmware 6.3.0 o successivo): comandi, lager 10 °C, riavvio, commutazioni della pompa, diacetile 17 °C, rampa, on/off senza isteresi, P, PI, PID, cold crash 2 °C, allarme freddo | nessuna |
          | Visita per il flash 6.4.1 | **Mancanza di corrente** (5-10 min) e flash via hotspot | sì, circa 20 minuti |
          | Dopo la 6.5.0 (da scrivere) | **Fase B** (6.5.0): sensore, relè, WiFi, watchdog — circa 3 ore | nessuna |
          | Prima della birra | Verdetto **pronto per la birra** e ritorno in Produzione | — |

      - type: entities
        title: Impostazioni
        entities:
          - entity: input_number.f1_collaudo_temp_parcheggio
            name: Temperatura a fine collaudo
          - entity: input_number.f1_v6_soglia_scostamento
            name: Soglia allarme scostamento
          - entity: input_number.f1_v6_minuti_scostamento
            name: Minuti prima dell'allarme
          - type: button
            name: Azzera i risultati
            icon: mdi:broom
            action_name: Azzera
            tap_action:
              action: perform-action
              perform_action: script.f1_collaudo_azzera
              confirmation:
                text: Cancellare tutti i risultati del collaudo?

      - type: markdown
        title: I test
        content: |
          {% for id in @@IDS@@ %}{% set a = states['sensor.f1_collaudo_' ~ id].attributes if states['sensor.f1_collaudo_' ~ id] else {} %}
          {% if a %}### {{ a.nome }}
          *{{ a.durata }} · firmware {{ a.firmware }}{% if a.presenza != 'nessuna' %} · **presenza:** {{ a.presenza }}{% endif %}*

          {{ a.descrizione }}

          {% for c in a.criteri %}- {{ c }}
          {% endfor %}
          {% endif %}{% endfor %}
"""

if __name__ == "__main__":
    main()
