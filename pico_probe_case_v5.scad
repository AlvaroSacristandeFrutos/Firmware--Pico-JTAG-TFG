// ============================================================
// Caja para Sonda JTAG — Raspberry Pi Pico
// Carcasa imprimible en 3D en dos piezas: base y tapa.
// El cierre se realiza mediante un labio perimetral a presión.
//
// Sistema de coordenadas:
//   Origen en la esquina superior izquierda de la PCB (vista usuario)
//   X → derecha,  Y → abajo  (coordenadas de usuario / esquemático)
//   En SCAD: X = userX,  Y = pcb_largo - userY,  Z = altura
//
//   Cara delantera = conector USB  (userY bajo  → scad Y alto)
//   Cara trasera   = conectores JTAG (userY alto → scad Y bajo)
// ============================================================

// === DIMENSIONES DE LA PCB ===
// Medidas físicas de la placa en mm
pcb_ancho  = 41.0;    // ancho de la PCB
pcb_largo  = 72.8;    // largo de la PCB
pcb_grosor = 1.6;     // grosor estándar FR4

// === TOLERANCIAS DE AJUSTE ===
// Holgura entre la PCB y las paredes interiores de la caja
pcb_holgura_x    = 0.5;   // holgura lateral (izquierda y derecha, simétrica)
// Holgura asimétrica en Y: más espacio en el lado JTAG porque los tornillos
// de montaje están más alejados del borde trasero de la PCB
pcb_holgura_del  = 0.0;   // holgura cara delantera (USB)
pcb_holgura_tras = 2.5;   // holgura cara trasera (JTAG)
tolerancia_impresion = 0.2; // tolerancia general de impresión 3D

// === GROSOR DE PAREDES ===
pared         = 3.6;   // grosor de las paredes laterales
suelo_grosor  = 1.5;   // grosor del suelo de la base
techo_grosor  = 1.5;   // grosor del techo de la tapa

// === ALTURAS Y POSICIÓN VERTICAL DE LA PCB ===
// Espacio libre bajo la PCB para componentes SMD en la cara inferior
altura_comp_inferior = 2;
// Posición Z de la cara superior de la PCB dentro de la caja
pcb_z = suelo_grosor + altura_comp_inferior + 0.5;

// Alturas máximas de los componentes montados sobre la PCB
pico_altura     = 15.0;   // Raspberry Pi Pico (incluye conector USB)
conector_altura = 8.5;    // conectores JTAG (IDC)
led_th_altura   = 9.0;    // LEDs de inserción (THT) de 3 mm

// Reducción global de la altura para un perfil más compacto
reduccion_caja = 8.0;

// Plano de corte entre base y tapa (Z absoluto de la línea de separación)
linea_corte = pcb_z + pcb_grosor + conector_altura + 0.4 - reduccion_caja;
// Altura interior total necesaria para alojar el Pico con su conector USB
techo_interior  = pcb_z + pcb_grosor + pico_altura + 0.4 - reduccion_caja;
// Profundidad interior de la tapa (espacio entre la línea de corte y el techo)
tapa_interior_h = techo_interior - linea_corte;

// Alturas finales de cada pieza
base_altura = linea_corte;
tapa_altura = techo_grosor + tapa_interior_h;

// === DIMENSIONES EXTERNAS DE LA CAJA ===
caja_ancho = pcb_ancho + 2*(pared + pcb_holgura_x);
caja_largo = pcb_largo + pcb_holgura_del + pcb_holgura_tras + 2*pared;

// ============================================================
// FUNCIONES DE TRANSFORMACIÓN DE COORDENADAS
// Convierten coordenadas del esquemático PCB (origen superior izquierdo)
// al espacio SCAD (origen en la esquina inferior del interior de la caja)
// ============================================================
function bx(ux) = ux + pared + pcb_holgura_x;
function by(uy) = (pcb_largo - uy) + pared + pcb_holgura_tras;

// === TORNILLOS Y COLUMNAS DE MONTAJE ===
// Los tornillos M3 fijan la PCB a las columnas de la base
tornillo_d        = 3.0;   // diámetro del vástago M3
cabeza_tornillo_d = 5.5;   // diámetro de la cabeza M3 (para avellanado)
soporte_d         = 6.5;   // diámetro exterior de las columnas de montaje
soporte_h         = pcb_z - suelo_grosor;  // altura de las columnas

// Posiciones de los taladros de montaje (coordenadas de usuario, en mm)
agujeros_montaje = [
    [4.1,  4.1],    // delantero izquierdo (lado USB)
    [36.8, 4.1],    // delantero derecho (lado USB)
    [4.2,  69.6],   // trasero izquierdo (lado JTAG)
    [36.8, 69.6],   // trasero derecho (lado JTAG)
];

// === POSICIÓN DE LOS LEDs ===
// Coordenadas del centro de cada LED (coordenadas de usuario)
led_rojo   = [10.4, 59.4];    // LED de estado rojo (THT 3 mm)
led_verde  = [10.4, 54.4];    // LED de estado verde (THT 3 mm)
led_pico   = [14.677, 4.577]; // LED de alimentación del Pico (SMD)
ventana_led_3mm  = 3.5;       // diámetro del hueco para LEDs THT
ventana_led_pico = 2.0;       // diámetro del hueco para el LED SMD

// === CONECTOR JTAG DE 20 PINES ===
// Conector IDC de 20 pines (paso 2.54 mm), interfaz JTAG estándar
jtag20_esq          = [7.55, 64.3];   // esquina superior izquierda del cuerpo
jtag20_cuerpo_ancho = 25.6;
jtag20_cuerpo_alto  = 5.3;
// Ranura en la tapa: holgura extra para facilitar la inserción del cable
jtag20_ranura_ancho = jtag20_cuerpo_ancho + 2 + 3;
jtag20_ranura_alto  = jtag20_cuerpo_alto  + 2 + 1;
// Centro geométrico del conector (para posicionar rebajes y etiquetas)
jtag20_cx = jtag20_esq[0] + jtag20_cuerpo_ancho/2;
jtag20_cy = jtag20_esq[1] + jtag20_cuerpo_alto/2;

// === CONECTOR JTAG DE 10 PINES ===
// Conector alternativo de paso 1.27 mm (ARM Cortex debug)
jtag10_esq          = [15.2, 58.1];
jtag10_cuerpo_ancho = 8.0;
jtag10_cuerpo_alto  = 3.8;
jtag10_ranura_ancho = jtag10_cuerpo_ancho + 2 + 3;
jtag10_ranura_alto  = jtag10_cuerpo_alto  + 2 + 1;
jtag10_cx = jtag10_esq[0] + jtag10_cuerpo_ancho/2;
jtag10_cy = jtag10_esq[1] + jtag10_cuerpo_alto/2;

// === CONECTOR UART ===
// Conector de depuración serie (puente UART transparente)
uart_esq          = [29.6, 52.7];
uart_cuerpo_ancho = 8.0;
uart_cuerpo_alto  = 3.8;
uart_ranura_ancho = uart_cuerpo_ancho + 2;
uart_ranura_alto  = uart_cuerpo_alto  + 2;
uart_cx = uart_esq[0] + uart_cuerpo_ancho/2;
uart_cy = uart_esq[1] + uart_cuerpo_alto/2;

// === CONECTOR SWD (pared lateral derecha) ===
// Puerto de depuración SWD del propio Pico; la abertura atraviesa la pared derecha
swd_esq           = [33.2, 55.7];
swd_abertura_ancho = 7.8;
swd_abertura_alto  = 7.8;
swd_cy = swd_esq[1] + swd_abertura_ancho/2;

// === CONECTOR USB (pared delantera) ===
// Hueco para el conector micro-USB del Raspberry Pi Pico
usb_esq           = [15.4, 0];
usb_abertura_ancho = 10.0;
usb_abertura_alto  = 5.0;
usb_cx = usb_esq[0] + usb_abertura_ancho/2;

// === PARÁMETROS DE GRABADO Y ETIQUETADO ===
rebaje_profundidad  = 0.6;   // profundidad de los rebajes alrededor de conectores
rebaje_margen       = 1.5;   // margen adicional de cada rebaje respecto al conector
grabado_profundidad = 0.4;   // profundidad del texto grabado en relieve
texto_grande  = 2.2;         // tamaño de fuente para etiquetas grandes (mm)
texto_mediano = 1.6;         // tamaño de fuente para etiquetas medianas (mm)
texto_pequeno = 1.3;         // tamaño de fuente para etiquetas pequeñas (mm)

// ============================================================
// LABIO PERIMETRAL DE CIERRE
// Anillo que sobresale de la base y encaja en la ranura de la tapa,
// garantizando la alineación y el cierre a presión de ambas piezas.
// ============================================================
labio_alto    = 2.5;   // altura del labio (profundidad de encaje)
labio_grosor  = 2.0;   // grosor de la pared del labio
labio_holgura = 0.2;   // juego entre labio y ranura para ajuste a presión

// ============================================================
// LÁMINAS DE ALINEACIÓN (desactivadas con *, código conservado)
// Pestañas adicionales que refuerzan la alineación tapa-base
// en los cuatro lados, complementando al labio perimetral.
// ============================================================
lamina_largo      = 14.0;    // longitud de las láminas laterales (izq/der)
lamina_del_ancho  = 7.0;     // anchura de las láminas delanteras (×2, flanqueando USB)
lamina_tras_largo = 11.0;    // longitud de la lámina trasera
lamina_alto       = 4.0;     // altura de cada lámina
lamina_grosor     = pared/2; // grosor de cada lámina
lamina_holgura    = 0.65;    // juego entre lámina y su alojamiento

// Dimensiones del espacio interior sin paredes (referencia)
interior_ancho = pcb_ancho + 2*pcb_holgura_x;
interior_largo = pcb_largo + pcb_holgura_del + pcb_holgura_tras;

// ============================================================
// MÓDULO AUXILIAR: caja_redondeada
// Genera un prisma rectangular con esquinas redondeadas mediante
// la operación hull() sobre cuatro cilindros en las esquinas.
// ============================================================
module caja_redondeada(ancho, largo, alto, r=2) {
    hull() {
        for (x=[r, ancho-r]) for (y=[r, largo-r])
            translate([x, y, 0]) cylinder(r=r, h=alto, $fn=24);
    }
}

// ============================================================
// MÓDULO: base()
// Pieza inferior de la carcasa. Incluye:
//   - Paredes y suelo con esquinas redondeadas
//   - Labio perimetral de cierre (parte macho)
//   - Columnas de montaje huecas que elevan la PCB del suelo
//   - Avellanados cónicos en el suelo para embutir las cabezas de tornillo
//   - Aberturas para el conector USB (pared delantera) y SWD (pared lateral)
// ============================================================
module base() {
    difference() {
        union() {
            // Cuerpo exterior de la base
            caja_redondeada(caja_ancho, caja_largo, base_altura, r=2);

            // Labio perimetral que encaja en la ranura de la tapa
            translate([pared - labio_grosor, pared - labio_grosor, base_altura])
                difference() {
                    caja_redondeada(
                        interior_ancho + 2*labio_grosor,
                        interior_largo + 2*labio_grosor,
                        labio_alto, r=1.5);
                    translate([labio_grosor, labio_grosor, -0.1])
                        caja_redondeada(
                            interior_ancho,
                            interior_largo,
                            labio_alto + 0.2, r=1);
                }

            // Láminas de alineación de la base (desactivadas)
            // Lámina izquierda
            *translate([pared/2, caja_largo/2 - lamina_largo/2, base_altura])
                cube([lamina_grosor, lamina_largo, lamina_alto]);
            // Láminas delanteras (×2, flanqueando el hueco USB)
            *translate([pared + 2, caja_largo - pared + pared/2, base_altura])
                cube([lamina_del_ancho, lamina_grosor, lamina_alto]);
            *translate([caja_ancho - pared - 2 - lamina_del_ancho,
                        caja_largo - pared + pared/2, base_altura])
                cube([lamina_del_ancho, lamina_grosor, lamina_alto]);
        }

        // Vaciado interior de la base
        translate([pared, pared, suelo_grosor])
            caja_redondeada(interior_ancho, interior_largo,
                            base_altura - suelo_grosor + 0.1, r=1);

        // Tornillos M3: taladro pasante + avellanado cónico en el suelo
        for (ag = agujeros_montaje) {
            // Taladro pasante para el vástago del tornillo
            translate([bx(ag[0]), by(ag[1]), -0.1])
                cylinder(d=tornillo_d + tolerancia_impresion,
                         h=suelo_grosor + 0.2, $fn=24);
            // Avellanado cónico para embutir la cabeza del tornillo rasante
            translate([bx(ag[0]), by(ag[1]), -0.1])
                cylinder(d1=cabeza_tornillo_d + tolerancia_impresion + 0.2,
                         d2=tornillo_d + tolerancia_impresion,
                         h=1.25 + 0.1, $fn=24);
        }

        // Hueco USB en la pared delantera (paso del conector micro-USB)
        translate([bx(usb_cx) - usb_abertura_ancho/2,
                   caja_largo - pared - 0.1,
                   pcb_z])
            cube([usb_abertura_ancho, pared + 0.2, base_altura - pcb_z + labio_alto + 0.1]);

        // Rebaje exterior del hueco USB (amplía el paso por fuera para el encapsulado)
        translate([bx(usb_cx) - (usb_abertura_ancho + 4.0)/2,
                   caja_largo - pared + labio_grosor,
                   pcb_z])
            cube([usb_abertura_ancho + 4.0, (pared - labio_grosor) + 0.2,
                  base_altura - pcb_z + labio_alto + 0.1]);

        // Hueco SWD en la pared lateral derecha
        translate([caja_ancho - pared - 0.1,
                   by(swd_cy) - swd_abertura_ancho/2,
                   pcb_z])
            cube([pared + 0.2, swd_abertura_ancho,
                  base_altura - pcb_z + labio_alto + 0.1]);

        // Alojamientos para las láminas de la tapa (desactivados)
        // Alojamiento lateral derecho
        *translate([caja_ancho - pared - lamina_holgura,
                    caja_largo/2 - lamina_largo/2 - lamina_holgura,
                    base_altura - lamina_alto - 0.1])
            cube([lamina_grosor + 2*lamina_holgura,
                  lamina_largo + 2*lamina_holgura,
                  lamina_alto + labio_alto + 0.2]);
        // Alojamiento trasero
        *translate([caja_ancho/2 - lamina_tras_largo/2 - lamina_holgura,
                    pared/2 - lamina_holgura,
                    base_altura - lamina_alto - 0.1])
            cube([lamina_tras_largo + 2*lamina_holgura,
                  lamina_grosor + 2*lamina_holgura,
                  lamina_alto + labio_alto + 0.2]);
    }

    // Columnas de montaje huecas que elevan la PCB sobre el suelo
    // El canal interior permite el paso del vástago del tornillo M3
    for (ag = agujeros_montaje)
        translate([bx(ag[0]), by(ag[1]), suelo_grosor])
            difference() {
                cylinder(d=soporte_d, h=soporte_h, $fn=24);
                translate([0, 0, -0.1])
                    cylinder(d=tornillo_d + tolerancia_impresion,
                             h=soporte_h + 0.2, $fn=24);
            };
}

// ============================================================
// MÓDULO: tapa()
// Pieza superior de la carcasa. Incluye:
//   - Cuerpo exterior con esquinas redondeadas
//   - Ranura perimetral interior que recibe el labio de la base
//   - Rebajes en la superficie exterior sobre los conectores JTAG y UART
//   - Aberturas pasantes para los LEDs (THT y SMD)
//   - Aberturas pasantes para los conectores JTAG 20 y JTAG 10
//   - Abertura para el conector UART
//   - Hueco USB en la pared delantera (complementa al de la base)
//   - Etiquetas grabadas en relieve (desactivadas, código conservado)
// ============================================================
module tapa() {
    th = techo_grosor + tapa_interior_h;  // altura total de la tapa
    rm = rebaje_margen;

    difference() {
        // Cuerpo exterior de la tapa
        caja_redondeada(caja_ancho, caja_largo, th, r=2);

        // Vaciado interior (ligeramente sobredimensionado para que la base
        // entre sin interferencias en el ensamblaje)
        translate([pared - 0.4, pared - 0.4, techo_grosor])
            caja_redondeada(interior_ancho + 0.8, interior_largo + 0.8,
                            tapa_interior_h + 0.1, r=1);

        // Ranura perimetral que recibe el labio de la base
        translate([pared - labio_grosor - labio_holgura,
                   pared - labio_grosor - labio_holgura,
                   th - labio_alto])
            caja_redondeada(
                interior_ancho + 2*(labio_grosor + labio_holgura),
                interior_largo + 2*(labio_grosor + labio_holgura),
                labio_alto + 0.1, r=1.5);

        // Alojamientos para las láminas de la base (desactivados)
        // Alojamiento lateral izquierdo
        *translate([pared/2 - lamina_holgura,
                    caja_largo/2 - lamina_largo/2 - lamina_holgura,
                    th - lamina_alto - 0.1])
            cube([lamina_grosor + 2*lamina_holgura,
                  lamina_largo + 2*lamina_holgura,
                  lamina_alto + 0.2]);
        // Alojamiento delantero (hueco continuo flanqueando el USB)
        *hull() {
            translate([pared + 2 - lamina_holgura,
                       caja_largo - pared + pared/2 - lamina_holgura,
                       th - lamina_alto - 0.1])
                cube([lamina_del_ancho + 2*lamina_holgura,
                      lamina_grosor + 2*lamina_holgura,
                      lamina_alto + 0.2]);
            translate([caja_ancho - pared - 2 - lamina_del_ancho - lamina_holgura,
                       caja_largo - pared + pared/2 - lamina_holgura,
                       th - lamina_alto - 0.1])
                cube([lamina_del_ancho + 2*lamina_holgura,
                      lamina_grosor + 2*lamina_holgura,
                      lamina_alto + 0.2]);
        }

        // Rebajes en la superficie exterior alrededor de los conectores
        // (zona hundida para evitar interferencias con el cuerpo del conector)
        // JTAG 20 y JTAG 10 se fusionan en un único rebaje continuo
        union() {
            translate([bx(jtag20_cx) - jtag20_ranura_ancho/2 - rm,
                       by(jtag20_cy) - jtag20_ranura_alto/2 - rm, -0.1])
                caja_redondeada(jtag20_ranura_ancho + 2*rm,
                                jtag20_ranura_alto + 2*rm,
                                rebaje_profundidad + 0.1, r=1);
            translate([bx(jtag10_cx) - jtag10_ranura_ancho/2 - rm,
                       by(jtag10_cy) - jtag10_ranura_alto/2 - rm, -0.1])
                caja_redondeada(jtag10_ranura_ancho + 2*rm,
                                jtag10_ranura_alto + 2*rm,
                                rebaje_profundidad + 0.1, r=1);
        }
        // Rebaje alrededor del conector UART
        translate([bx(uart_cx) - uart_ranura_ancho/2 - rm,
                   by(uart_cy) - uart_ranura_alto/2 - rm, -0.1])
            caja_redondeada(uart_ranura_ancho + 2*rm,
                            uart_ranura_alto + 2*rm,
                            rebaje_profundidad + 0.1, r=1);

        // Aberturas pasantes en la superficie de la tapa
        // Ventana para el LED verde (THT 3 mm)
        translate([bx(led_verde[0]), by(led_verde[1]), -0.1])
            cylinder(d=ventana_led_3mm, h=techo_grosor + 0.2, $fn=24);

        // Ventana para el LED rojo: se elimina el material sobrante entre el hueco
        // rectangular y el cilindro para evitar islas de plástico sin soporte
        translate([bx(led_rojo[0]) - 1.9, by(led_rojo[1]) - 4.35, -0.1])
            cube([4.5, 3.3, techo_grosor + 0.2]);
        translate([bx(led_rojo[0]), by(led_rojo[1]), -0.1])
            cylinder(d=6, h=techo_grosor + 0.2, $fn=24);

        // Ventana para el LED de alimentación del Pico (SMD)
        translate([bx(led_pico[0]), by(led_pico[1]), -0.1])
            cylinder(d=ventana_led_pico, h=techo_grosor + 0.2, $fn=24);

        // Aberturas pasantes para los conectores JTAG 20 y JTAG 10 (hueco fusionado)
        union() {
            translate([bx(jtag20_cx) - jtag20_ranura_ancho/2,
                       by(jtag20_cy) - jtag20_ranura_alto/2, -0.1])
                cube([jtag20_ranura_ancho, jtag20_ranura_alto, techo_grosor + 0.2]);
            translate([bx(jtag10_cx) - jtag10_ranura_ancho/2,
                       by(jtag10_cy) - jtag10_ranura_alto/2, -0.1])
                cube([jtag10_ranura_ancho, jtag10_ranura_alto, techo_grosor + 0.2]);
        }

        // Abertura pasante para el conector UART
        translate([bx(uart_cx) - uart_ranura_ancho/2,
                   by(uart_cy) - uart_ranura_alto/2, -0.1])
            cube([uart_ranura_ancho, uart_ranura_alto, techo_grosor + 0.2]);

        // Hueco USB en la pared delantera de la tapa
        // (el conector micro-USB sobresale y requiere paso en ambas piezas)
        translate([bx(usb_cx) - (usb_abertura_ancho + 4.0)/2,
                   caja_largo - pared - 0.1, th - 6.0])
            cube([usb_abertura_ancho + 4.0, pared + 0.2, 6.1]);

        // Hueco USB alternativo de apertura completa (desactivado)
        *translate([bx(usb_cx) - usb_abertura_ancho/2,
                    caja_largo - pared - 0.1, 0])
            cube([usb_abertura_ancho, pared + 0.2, th + 0.1]);

        // Hueco SWD en la pared lateral derecha (continúa el hueco de la base)
        translate([caja_ancho - pared - 0.1,
                   by(swd_cy) - swd_abertura_ancho/2,
                   techo_grosor])
            cube([pared + 0.2, swd_abertura_ancho, tapa_interior_h + 0.1]);

        // Etiquetas grabadas en relieve sobre la superficie de la tapa
        // (desactivadas; código conservado para activar si se desea identificar conectores)
        *translate([bx(jtag10_cx),
                    by(jtag10_cy) + jtag10_ranura_alto/2 + rm + 2,
                    grabado_profundidad])
            mirror([0,0,1]) linear_extrude(grabado_profundidad + 0.1)
                text("JTAG 10", size=texto_mediano, halign="center",
                     valign="center", font="Liberation Sans:style=Bold");
        *translate([bx(uart_cx),
                    by(uart_cy) + uart_ranura_alto/2 + rm + 2,
                    grabado_profundidad])
            mirror([0,0,1]) linear_extrude(grabado_profundidad + 0.1)
                text("UART", size=texto_mediano, halign="center",
                     valign="center", font="Liberation Sans:style=Bold");
        *translate([bx(usb_cx), caja_largo - pared - 5, grabado_profundidad])
            mirror([0,0,1]) linear_extrude(grabado_profundidad + 0.1)
                text("USB", size=texto_mediano, halign="center",
                     valign="center", font="Liberation Sans:style=Bold");
        *translate([bx(led_verde[0]) - ventana_led_3mm/2 - 0.8,
                    by(led_verde[1]), grabado_profundidad])
            mirror([0,0,1]) linear_extrude(grabado_profundidad + 0.1)
                text("G", size=texto_mediano, halign="right",
                     valign="center", font="Liberation Sans:style=Bold");
        *translate([bx(led_rojo[0]) - ventana_led_3mm/2 - 0.8,
                    by(led_rojo[1]), grabado_profundidad])
            mirror([0,0,1]) linear_extrude(grabado_profundidad + 0.1)
                text("R", size=texto_mediano, halign="right",
                     valign="center", font="Liberation Sans:style=Bold");
        *translate([bx(led_pico[0]) - ventana_led_pico/2 - 0.8,
                    by(led_pico[1]), grabado_profundidad])
            mirror([0,0,1]) linear_extrude(grabado_profundidad + 0.1)
                text("PWR", size=texto_pequeno, halign="right",
                     valign="center", font="Liberation Sans");
        // Etiqueta "JTAG 20" en la pared trasera exterior (desactivada)
        *translate([bx(jtag20_cx), grabado_profundidad, th/2])
            rotate([90, 180, 0])
                linear_extrude(grabado_profundidad + 0.1)
                    mirror([1, 0, 0])
                        text("JTAG 20", size=texto_grande, halign="center",
                             valign="center", font="Liberation Sans:style=Bold");
    }

    // Láminas de alineación que descienden desde la tapa (desactivadas)
    // Lámina lateral derecha
    *translate([caja_ancho - pared, caja_largo/2 - lamina_largo/2, th])
        cube([lamina_grosor, lamina_largo, lamina_alto]);
    // Lámina trasera
    *translate([caja_ancho/2 - lamina_tras_largo/2, pared/2, th])
        cube([lamina_tras_largo, lamina_grosor, lamina_alto]);

    // Etiqueta "SWD" en relieve en la pared lateral exterior (desactivada)
    *translate([caja_ancho, by(swd_cy) + swd_abertura_ancho/2 + 4, th/2])
        rotate([270, 0, 90])
            linear_extrude(0.4)
                text("SWD", size=texto_mediano, halign="center",
                     valign="center", font="Liberation Sans:style=Bold");
}

// ============================================================
// VISTA DE ENSAMBLAJE (desactivada con *)
// Muestra las dos piezas separadas para verificar el encaje.
// También incluye la PCB y los LEDs como referencia transparente.
// ============================================================
separacion = 15;  // separación vertical entre piezas en la vista explosionada

*color("DimGray", 0.85) base();
*color("DarkSlateGray", 0.65)
    translate([0, 0, base_altura + separacion])
        mirror([0, 0, 1])
            translate([0, 0, -tapa_altura])
                tapa();

// PCB de referencia (transparente, para verificar holguras y alineación)
*color("Green", 0.2)
    translate([pared + pcb_holgura_x, pared + pcb_holgura_tras, pcb_z])
        cube([pcb_ancho, pcb_largo, pcb_grosor]);
// LEDs de referencia (transparentes)
*color("Lime", 0.4)
    translate([bx(led_verde[0]), by(led_verde[1]), pcb_z + pcb_grosor])
        cylinder(d=3, h=led_th_altura, $fn=16);
*color("Red", 0.4)
    translate([bx(led_rojo[0]), by(led_rojo[1]), pcb_z + pcb_grosor])
        cylinder(d=3, h=led_th_altura, $fn=16);

// ============================================================
// LAYOUT DE IMPRESIÓN
// Ambas piezas posicionadas para impresión 3D directa sin soportes:
//   - Base: posición normal con el suelo sobre la cama de impresión
//   - Tapa: desplazada lateralmente y espejada en X, de modo que el techo
//     quede sobre la cama (boca abajo) y los huecos de conectores queden
//     correctamente orientados al cerrar la carcasa
// ============================================================

// Pieza 1: base en posición de impresión
base();

// Pieza 2: tapa boca abajo, al lado de la base
translate([caja_ancho * 2 + 10, 0, 0])
    mirror([1, 0, 0])
        tapa();
