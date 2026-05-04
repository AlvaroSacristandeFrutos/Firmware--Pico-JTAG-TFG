// ============================================================
// Caja para Sonda JTAG — Raspberry Pi Pico  v15
// Labio perimetral + láminas 4 lados + holguras asimétricas
// Ajustes v15: encaje tapa-base corregido, base +3mm,
// tornillos a 65.5mm, hull() huecos delanteros, etiquetas off
// ============================================================
// COORDS: origen sup-izq Pico, X→dcha, Y→abajo
// SCAD:   X=userX, Y=pcb_largo-userY, Z=altura
//
// "Delantera" = Pico/USB (user Y bajo) = scad Y alto
// "Trasera"   = JTAG (user Y alto)     = scad Y bajo
// ============================================================

// === PCB ===
pcb_ancho = 41.0;
pcb_largo = 72.8;
pcb_grosor = 1.6;

// === TOLERANCIAS ===
pcb_holgura_x = 0.5;
// Holguras Y ASIMÉTRICAS para cumplir distancias a pared:
// Delantera (Pico): tornillo a 5.4mm → 5.4 - 4.1 = 1.3mm
// Trasera (JTAG):  tornillo a 7.4mm → 7.4 - 0.9 = 6.5mm
pcb_holgura_del  = 0.0;   // lado Pico/USB (scad Y alto)
pcb_holgura_tras = 6.5;   // lado JTAG (scad Y bajo)
tolerancia_impresion = 0.2;

// === CAJA ===
pared = 2.2;
suelo_grosor = 1.5;
techo_grosor = 1.5;

// === ALTURAS ===
altura_comp_inferior = 3.5;
pcb_z = suelo_grosor + altura_comp_inferior + 0.5;
pico_altura     = 15.0;
conector_altura = 8.5;
led_th_altura   = 9.0;

// Reducción de altura aplicada a la base (-7mm).
// Se aplica también a techo_interior para que tapa_interior_h
// (y por tanto tapa_altura) no cambie respecto a v13.
reduccion_caja  = 5.0;

linea_corte     = pcb_z + pcb_grosor + conector_altura + 1.0 - reduccion_caja;
techo_interior  = pcb_z + pcb_grosor + pico_altura + 1.0 - reduccion_caja;
tapa_interior_h = techo_interior - linea_corte;

base_altura = linea_corte;
tapa_altura = techo_grosor + tapa_interior_h;

// === DIMENSIONES EXTERNAS ===
caja_ancho = pcb_ancho + 2*(pared + pcb_holgura_x);
caja_largo = pcb_largo + pcb_holgura_del + pcb_holgura_tras + 2*pared;

// ============================================================
// FUNCIONES DE COORDENADAS
// ============================================================
// PCB empieza en scad Y = pared + pcb_holgura_tras
function bx(ux) = ux + pared + pcb_holgura_x;
function by(uy) = (pcb_largo - uy) + pared + pcb_holgura_tras;

// === TORNILLOS Y SOPORTES (coordenadas ORIGINALES de la PCB) ===
tornillo_d = 3.0;
cabeza_tornillo_d = 5.5;
soporte_d = 6.5;
soporte_h = pcb_z - suelo_grosor;

agujeros_montaje = [
    [4.1,  4.1],    // Delantera izquierda (Pico) - FIJO
    [36.8, 4.1],    // Delantera derecha (Pico)   - FIJO
    [4.2,  69.6],   // Trasera izquierda (JTAG)   - Movido a 65.5mm del delantero
    [36.8, 69.6],   // Trasera derecha (JTAG)     - Movido a 65.5mm del delantero
];

// === LEDS ===
led_rojo   = [10.4, 59.4];
led_verde  = [10.4, 54.4];
led_pico   = [14.677, 4.577];
ventana_led_3mm  = 3.5;
ventana_led_pico = 2.0;

// === JTAG 20 PINES ===
jtag20_esq = [7.55, 64.3];
jtag20_cuerpo_ancho = 25.6;
jtag20_cuerpo_alto  = 5.3;
jtag20_ranura_ancho = jtag20_cuerpo_ancho + 2 + 3;  // +3mm extra X
jtag20_ranura_alto  = jtag20_cuerpo_alto + 2 + 1;   // +1mm extra Y
jtag20_cx = jtag20_esq[0] + jtag20_cuerpo_ancho/2;
jtag20_cy = jtag20_esq[1] + jtag20_cuerpo_alto/2;

// === JTAG 10 PINES ===
jtag10_esq = [15.2, 58.1];
jtag10_cuerpo_ancho = 8.0;
jtag10_cuerpo_alto  = 3.8;
jtag10_ranura_ancho = jtag10_cuerpo_ancho + 2 + 3;  // +3mm extra X
jtag10_ranura_alto  = jtag10_cuerpo_alto + 2 + 1;   // +1mm extra Y
jtag10_cx = jtag10_esq[0] + jtag10_cuerpo_ancho/2;
jtag10_cy = jtag10_esq[1] + jtag10_cuerpo_alto/2;

// === UART ===
uart_esq = [29.6, 52.7];
uart_cuerpo_ancho = 8.0;
uart_cuerpo_alto  = 3.8;
uart_ranura_ancho = uart_cuerpo_ancho + 2;
uart_ranura_alto  = uart_cuerpo_alto + 2;
uart_cx = uart_esq[0] + uart_cuerpo_ancho/2;
uart_cy = uart_esq[1] + uart_cuerpo_alto/2;

// === SWD (pared derecha) ===
swd_esq = [33.2, 55.7];
swd_abertura_ancho = 7.8;
swd_abertura_alto  = 7.8;
swd_cy = swd_esq[1] + swd_abertura_ancho/2;

// === USB (pared delantera) ===
usb_esq = [15.4, 0];
usb_abertura_ancho = 10.0;
usb_abertura_alto  = 5.0;
usb_cx = usb_esq[0] + usb_abertura_ancho/2;

// === REBAJES Y TEXTO ===
rebaje_profundidad = 0.6;
rebaje_margen = 1.5;
grabado_profundidad = 0.4;
texto_grande  = 2.2;
texto_mediano = 1.6;
texto_pequeno = 1.3;

// ============================================================
// LABIO PERIMETRAL
// ============================================================
labio_alto    = 2.5;
labio_grosor  = 1.0;
labio_holgura = 0.4;   // v15: 0.15 → 0.4 para encaje a presión correcto

// ============================================================
// LÁMINAS DE ALINEACIÓN (4 lados)
// v14: pestañas reducidas en 1mm (-0.5mm por lado) y holgura
//      ampliada a 0.65 para absorber tolerancias de impresión
// ============================================================
lamina_largo    = 14.0;    // laterales (izq/der) — antes 15.0
lamina_del_ancho = 7.0;    // delantera (2 piezas flanqueando USB) — antes 8.0
lamina_tras_largo = 11.0;  // trasera — antes 12.0
lamina_alto     = 4.0;
lamina_grosor   = pared/2;
lamina_holgura  = 0.65;    // antes 0.15 (+0.5mm por lado de holgura)

// Interior ancho/largo (para referencia)
interior_ancho = pcb_ancho + 2*pcb_holgura_x;
interior_largo = pcb_largo + pcb_holgura_del + pcb_holgura_tras;

// ============================================================
// MÓDULOS AUXILIARES
// ============================================================
module caja_redondeada(ancho, largo, alto, r=2) {
    hull() {
        for (x=[r, ancho-r]) for (y=[r, largo-r])
            translate([x, y, 0]) cylinder(r=r, h=alto, $fn=24);
    }
}

// ============================================================
// BASE
// ============================================================
module base() {
    difference() {
        union() {
            caja_redondeada(caja_ancho, caja_largo, base_altura, r=2);
            
            // LABIO PERIMETRAL
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
            
            // LÁMINAS (suben desde la base)
            // Izquierda
            *translate([pared/2, caja_largo/2 - lamina_largo/2, base_altura])
                cube([lamina_grosor, lamina_largo, lamina_alto]);
            // Delantera: 2 piezas flanqueando USB (scad Y alto)
            *translate([pared + 2,
                       caja_largo - pared + pared/2,
                       base_altura])
                cube([lamina_del_ancho, lamina_grosor, lamina_alto]);
            *translate([caja_ancho - pared - 2 - lamina_del_ancho,
                       caja_largo - pared + pared/2,
                       base_altura])
                cube([lamina_del_ancho, lamina_grosor, lamina_alto]);
        }
        
        // Vaciado interior
        translate([pared, pared, suelo_grosor])
            caja_redondeada(interior_ancho, interior_largo,
                            base_altura - suelo_grosor + 0.1, r=1);
        
        // Tornillos M3
        for (ag = agujeros_montaje) {
            translate([bx(ag[0]), by(ag[1]), -0.1])
                cylinder(d=tornillo_d + tolerancia_impresion,
                         h=suelo_grosor + 0.2, $fn=24);
            translate([bx(ag[0]), by(ag[1]), -0.1])
                cylinder(d=cabeza_tornillo_d + tolerancia_impresion,
                         h=suelo_grosor, $fn=24);
        }
        
        // USB — pared delantera (hasta labio)
        translate([bx(usb_cx) - usb_abertura_ancho/2,
                   caja_largo - pared - 0.1,
                   pcb_z])
            cube([usb_abertura_ancho, pared + 0.2,
                  base_altura - pcb_z + labio_alto + 0.1]);
        
        // SWD — pared derecha (hasta labio)
        translate([caja_ancho - pared - 0.1,
                   by(swd_cy) - swd_abertura_ancho/2,
                   pcb_z])
            cube([pared + 0.2, swd_abertura_ancho,
                  base_altura - pcb_z + labio_alto + 0.1]);
        
        // HUECOS PARA LÁMINAS DE LA TAPA (derecha + trasera)
        // FIX: altura extendida con labio_alto para que no bloquee
        // Derecha
        *translate([caja_ancho - pared - lamina_holgura,
                   caja_largo/2 - lamina_largo/2 - lamina_holgura,
                   base_altura - lamina_alto - 0.1])
            cube([lamina_grosor + 2*lamina_holgura,
                  lamina_largo + 2*lamina_holgura,
                  lamina_alto + labio_alto + 0.2]);
        // Trasera (scad Y bajo) — FIX: extender con labio_alto
        *translate([caja_ancho/2 - lamina_tras_largo/2 - lamina_holgura,
                   pared/2 - lamina_holgura,
                   base_altura - lamina_alto - 0.1])
            cube([lamina_tras_largo + 2*lamina_holgura,
                  lamina_grosor + 2*lamina_holgura,
                  lamina_alto + labio_alto + 0.2]);
    }
    
    // Soportes para la PCB
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
// TAPA
// ============================================================
module tapa() {
    th = techo_grosor + tapa_interior_h;
    rm = rebaje_margen;
    
    difference() {
        // v15: contorno exterior revertido al original.
        // El crecimiento se hace ahora en el vaciado interior para
        // que la base entre realmente dentro de la tapa.
        caja_redondeada(caja_ancho, caja_largo, th, r=2);
        
        // Vaciado interior — v15: +0.8mm en X e Y, centrado
        translate([pared - 0.4, pared - 0.4, techo_grosor])
            caja_redondeada(interior_ancho + 0.8, interior_largo + 0.8,
                            tapa_interior_h + 0.1, r=1);
        
        // RANURA PARA LABIO PERIMETRAL
        translate([pared - labio_grosor - labio_holgura,
                   pared - labio_grosor - labio_holgura,
                   th - labio_alto])
            caja_redondeada(
                interior_ancho + 2*(labio_grosor + labio_holgura),
                interior_largo + 2*(labio_grosor + labio_holgura),
                labio_alto + 0.1, r=1.5);
        
        // HUECOS LÁMINAS BASE (izquierda + delantera×2)
        // Izquierda
        *translate([pared/2 - lamina_holgura,
                   caja_largo/2 - lamina_largo/2 - lamina_holgura,
                   th - lamina_alto - 0.1])
            cube([lamina_grosor + 2*lamina_holgura,
                  lamina_largo + 2*lamina_holgura,
                  lamina_alto + 0.2]);
        // Delantera USB (v15: hueco único con hull() que elimina el labio central)
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
        
        // === REBAJES ===
        // v14: JTAG 20 + JTAG 10 fusionados en un único rebaje continuo
        // (union de los dos rectángulos manteniendo posiciones originales).
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
        translate([bx(uart_cx) - uart_ranura_ancho/2 - rm,
                   by(uart_cy) - uart_ranura_alto/2 - rm, -0.1])
            caja_redondeada(uart_ranura_ancho + 2*rm,
                            uart_ranura_alto + 2*rm,
                            rebaje_profundidad + 0.1, r=1);
        
        // === ABERTURAS SUPERFICIE ===
        translate([bx(led_verde[0]), by(led_verde[1]), -0.1])
            cylinder(d=ventana_led_3mm, h=techo_grosor + 0.2, $fn=24);
        //hueco LED rojo ampliado a 3.8x2.7 (+0.5mm por eje)
        // y centrado en X y en Y respecto a led_rojo.
        translate([bx(led_rojo[0]) - 1.9, by(led_rojo[1]) - 1.35, -0.1])
            cube([3.8, 2.7, techo_grosor + 0.2]);
            //cylinder(d=ventana_led_3mm, h=techo_grosor + 0.2, $fn=24);
        translate([bx(led_pico[0]), by(led_pico[1]), -0.1])
            cylinder(d=ventana_led_pico, h=techo_grosor + 0.2, $fn=24);
        
        // aberturas JTAG 20 + JTAG 10 fusionadas en un único hueco
        // (union de los dos rectángulos manteniendo posiciones originales).
        union() {
            translate([bx(jtag20_cx) - jtag20_ranura_ancho/2,
                       by(jtag20_cy) - jtag20_ranura_alto/2, -0.1])
                cube([jtag20_ranura_ancho, jtag20_ranura_alto, techo_grosor + 0.2]);
            translate([bx(jtag10_cx) - jtag10_ranura_ancho/2,
                       by(jtag10_cy) - jtag10_ranura_alto/2, -0.1])
                cube([jtag10_ranura_ancho, jtag10_ranura_alto, techo_grosor + 0.2]);
        }
        translate([bx(uart_cx) - uart_ranura_ancho/2,
                   by(uart_cy) - uart_ranura_alto/2, -0.1])
            cube([uart_ranura_ancho, uart_ranura_alto, techo_grosor + 0.2]);
        
        // === ABERTURAS PAREDES ===
        *translate([bx(usb_cx) - usb_abertura_ancho/2,
                   caja_largo - pared - 0.1, 0])
            cube([usb_abertura_ancho, pared + 0.2, th + 0.1]);
        translate([caja_ancho - pared - 0.1,
                   by(swd_cy) - swd_abertura_ancho/2, 0])
            cube([pared + 2.0, swd_abertura_ancho, th + 0.1]);  
        
        // === ETIQUETAS === (v15: desactivadas con * — el código se conserva)
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
        
        // JTAG 20 grabado en pared trasera (scad Y=0) (v15: desactivado con *)
        *translate([bx(jtag20_cx), grabado_profundidad, th/2])
            rotate([90, 180, 0])
                linear_extrude(grabado_profundidad + 0.1)
                    mirror([1, 0, 0])
                        text("JTAG 20", size=texto_grande, halign="center",
                             valign="center", font="Liberation Sans:style=Bold");
    }
    
    // LÁMINAS (bajan desde la tapa)
    // Derecha (v15: desactivada con * para no imprimir)
    *translate([caja_ancho - pared,
               caja_largo/2 - lamina_largo/2, th])
        cube([lamina_grosor, lamina_largo, lamina_alto]);
    // Trasera (scad Y bajo)
    *translate([caja_ancho/2 - lamina_tras_largo/2,
               pared/2, th])
        cube([lamina_tras_largo, lamina_grosor, lamina_alto]);
    
    // SWD relieve (v15: desactivado con *)
    *translate([caja_ancho, by(swd_cy) + swd_abertura_ancho/2 + 4, th/2])
        rotate([270, 0, 90])
            linear_extrude(0.4)
                text("SWD", size=texto_mediano, halign="center",
                     valign="center", font="Liberation Sans:style=Bold");
}

// ============================================================
// ENSAMBLAJE (vista explosionada)
// ============================================================
separacion = 15;

color("DimGray", 0.85) base();
*color("DarkSlateGray", 0.65)
    translate([0, 0, base_altura + separacion])
        mirror([0, 0, 1])
            translate([0, 0, -tapa_altura])
                tapa();

// PCB referencia
*color("Green", 0.2)
    translate([pared + pcb_holgura_x,
               pared + pcb_holgura_tras,
               pcb_z])
        cube([pcb_ancho, pcb_largo, pcb_grosor]);
// LEDs referencia
*color("Lime", 0.4)
    translate([bx(led_verde[0]), by(led_verde[1]), pcb_z + pcb_grosor])
        cylinder(d=3, h=led_th_altura, $fn=16);
*color("Red", 0.4)
    translate([bx(led_rojo[0]), by(led_rojo[1]), pcb_z + pcb_grosor])
        cylinder(d=3, h=led_th_altura, $fn=16);

// ============================================================
// LAYOUT DE IMPRESIÓN (descomentar para imprimir)
// ============================================================
// Ambas piezas tocando Z=0, tapa boca abajo al lado de la base:
//
base();

// 2. La tapa se voltea boca abajo (techo en la cama, sin soportes).
// Al rotar 180º en Y (0, 180, 0), se gira "como un libro".
// El frente y la trasera quedan alineados exactamente igual que la base, 
// por lo que los agujeros coincidirán perfectamente al cerrarla.
base();

translate([caja_ancho * 2 + 10, 0, 0])
    mirror([1, 0, 0])
        tapa();
//

