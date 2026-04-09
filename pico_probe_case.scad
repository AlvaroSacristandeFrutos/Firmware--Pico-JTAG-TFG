// ============================================================
// Caja para Sonda JTAG — Raspberry Pi Pico  v11
// Bugs corregidos + variables en castellano
// ============================================================
// COORDS: origen sup-izq Pico, X→dcha, Y→abajo
// SCAD:   X=userX, Y=pcb_largo-userY, Z=altura
// ============================================================

// === PCB ===
pcb_ancho = 41.0;
pcb_largo = 72.8;
pcb_grosor = 1.6;

// === TOLERANCIAS ===
pcb_holgura_x = 0.5;
pcb_holgura_y = 3.0;      // mayor: los standoffs inferiores sobresalen 2.4mm
tolerancia_impresion = 0.2;

// === CAJA ===
pared = 1.8;
suelo_grosor = 1.5;
techo_grosor = 1.5;

// === ALTURAS ===
altura_comp_inferior = 3.5;   // level shifters bajo la PCB
pcb_z = suelo_grosor + altura_comp_inferior + 0.5;
pico_altura    = 15.0;
conector_altura = 8.5;
led_th_altura  = 9.0;

// Línea de corte base/tapa
linea_corte = pcb_z + pcb_grosor + conector_altura + 1.0;
techo_interior = pcb_z + pcb_grosor + pico_altura + 1.0;
tapa_interior_h = techo_interior - linea_corte;

base_altura = linea_corte;
tapa_altura = techo_grosor + tapa_interior_h;

// === DIMENSIONES EXTERNAS ===
caja_ancho = pcb_ancho + 2*(pared + pcb_holgura_x);
caja_largo = pcb_largo + 2*(pared + pcb_holgura_y);

// ============================================================
// FUNCIONES DE COORDENADAS
// ============================================================
function bx(ux) = ux + pared + pcb_holgura_x;
function by(uy) = (pcb_largo - uy) + pared + pcb_holgura_y;

// === TORNILLOS Y SOPORTES ===
tornillo_d = 3.0;
cabeza_tornillo_d = 5.5;
soporte_d = 6.5;
soporte_h = pcb_z - suelo_grosor;

agujeros_montaje = [[4.1,4.1],[36.8,4.1],[4.2,71.9],[36.8,71.8]];

// === LEDS ===
led_rojo   = [10.4, 54.4];
led_verde  = [10.4, 49.4];
led_pico   = [14.677, 4.577];
ventana_led_3mm = 3.5;
ventana_led_pico = 1.5;

// === JTAG 20 PINES ===
jtag20_esq = [7.55, 64.3];
jtag20_cuerpo_ancho = 25.6;
jtag20_cuerpo_alto = 5.3;
jtag20_ranura_ancho = jtag20_cuerpo_ancho + 2;
jtag20_ranura_alto = jtag20_cuerpo_alto + 2;
jtag20_cx = jtag20_esq[0] + jtag20_cuerpo_ancho/2;
jtag20_cy = jtag20_esq[1] + jtag20_cuerpo_alto/2;

// === JTAG 10 PINES ===
jtag10_esq = [15.2, 58.1];
jtag10_cuerpo_ancho = 8.0;
jtag10_cuerpo_alto = 3.8;
jtag10_ranura_ancho = jtag10_cuerpo_ancho + 2;
jtag10_ranura_alto = jtag10_cuerpo_alto + 2;
jtag10_cx = jtag10_esq[0] + jtag10_cuerpo_ancho/2;
jtag10_cy = jtag10_esq[1] + jtag10_cuerpo_alto/2;

// === UART ===
uart_esq = [29.6, 52.7];
uart_cuerpo_ancho = 8.0;
uart_cuerpo_alto = 3.8;
uart_ranura_ancho = uart_cuerpo_ancho + 2;
uart_ranura_alto = uart_cuerpo_alto + 2;
uart_cx = uart_esq[0] + uart_cuerpo_ancho/2;
uart_cy = uart_esq[1] + uart_cuerpo_alto/2;

// === SWD (pared derecha) ===
swd_esq = [33.2, 55.7];
swd_abertura_ancho = 7.8;
swd_abertura_alto = 7.8;
swd_cy = swd_esq[1] + swd_abertura_ancho/2;

// === USB (pared superior) ===
usb_esq = [16.4, 0];
usb_abertura_ancho = 8.0;
usb_abertura_alto = 5.0;
usb_cx = usb_esq[0] + usb_abertura_ancho/2;

// === REBAJES Y TEXTO ===
rebaje_profundidad = 0.6;
rebaje_margen = 1.5;
grabado_profundidad = 0.4;
texto_grande = 2.2;
texto_mediano = 1.6;
texto_pequeno = 1.3;

// ============================================================
// PESTAÑAS SNAP-FIT (arponcillo con refuerzo)
// ============================================================
pestana_ancho     = 7.0;
pestana_brazo_grosor = pared/2;     // media pared
pestana_largo     = 8.0;            // longitud total brazo
pestana_saliente  = 0.8;            // protuberancia del arponcillo
pestana_rampa     = 3.0;            // longitud de la rampa
pestana_holgura   = 0.2;
pestana_filete    = 7.0;            // longitud del refuerzo triangular
pestana_enganche_z = 3.0;           // posición Z del enganche
cubo_refuerzo_alto = 5.0;
cubo_refuerzo_z    = -4.5;          // posición Z (negativo = dentro de la tapa)

module pestana_arponc() {
    // Cubo de refuerzo (conecta pestaña con cuerpo de la tapa)
    translate([0, 0, cubo_refuerzo_z])
        cube([pestana_ancho, pared, cubo_refuerzo_alto]);
    
    // Filete triangular (Z=0 a Z=filete)
    // Transición de grosor completo de pared a media pared
    hull() {
        cube([pestana_ancho, pared, 0.01]);
        translate([0, 0, pestana_filete])
            cube([pestana_ancho, pestana_brazo_grosor, 0.01]);
    }
    
    // Brazo principal (Z=filete a Z=largo)
    translate([0, 0, pestana_filete])
        cube([pestana_ancho, pestana_brazo_grosor, pestana_largo - pestana_filete]);
    
    // Cara vertical del arponcillo (enganche)
    translate([0, -pestana_saliente, pestana_enganche_z])
        cube([pestana_ancho, pestana_brazo_grosor + pestana_saliente, 0.8]);
    
    // Rampa desde enganche hasta flush (guía de inserción)
    translate([0, 0, pestana_enganche_z + 0.8])
        hull() {
            translate([0, -pestana_saliente, 0])
                cube([pestana_ancho, pestana_brazo_grosor + pestana_saliente, 0.01]);
            translate([0, 0, pestana_rampa])
                cube([pestana_ancho, pestana_brazo_grosor, 0.01]);
        }
}

// Posiciones X de las pestañas en las esquinas
pestana_x_izq = pared + pcb_holgura_x + 2;
pestana_x_der = caja_ancho - pared - pcb_holgura_x - pestana_ancho - 2;

// Canal en la base: vaciado DENTRO de la pared para el saliente
// Estante = pared sólida por encima del canal que retiene el enganche
canal_altura = pestana_largo + 1 - pestana_enganche_z;
canal_z      = base_altura - pestana_largo - 1;

// ============================================================
// LÁMINAS DE ALINEACIÓN (media pared)
// ============================================================
lamina_largo   = 15.0;
lamina_alto    = 4.0;
lamina_grosor  = pared/2;
lamina_holgura = 0.15;

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
            // Lámina alineación IZQUIERDA (mitad interior de la pared)
            translate([pared/2, caja_largo/2 - lamina_largo/2, base_altura])
                cube([lamina_grosor, lamina_largo, lamina_alto]);
        }
        
        // Vaciado interior
        translate([pared, pared, suelo_grosor])
            caja_redondeada(pcb_ancho + 2*pcb_holgura_x,
                            pcb_largo + 2*pcb_holgura_y,
                            base_altura - suelo_grosor + 0.1, r=1);
        
        // Tornillos M3 (pasantes + avellanado)
        for (ag = agujeros_montaje) {
            // Pasante
            translate([bx(ag[0]), by(ag[1]), -0.1])
                cylinder(d=tornillo_d + tolerancia_impresion,
                         h=suelo_grosor + 0.2, $fn=24);
            // Avellanado (FIX: no más profundo que el suelo)
            translate([bx(ag[0]), by(ag[1]), -0.1])
                cylinder(d=cabeza_tornillo_d + tolerancia_impresion,
                         h=suelo_grosor, $fn=24);
        }
        
        // === ABERTURAS EN PAREDES (FIX: extendidas hasta base_altura) ===
        
        // USB — pared superior (extendida hasta arriba)
        translate([bx(usb_cx) - usb_abertura_ancho/2,
                   caja_largo - pared - 0.1,
                   pcb_z])
            cube([usb_abertura_ancho, pared + 0.2,
                  base_altura - pcb_z + 0.1]);
        
        // SWD — pared derecha (extendida hasta arriba)
        translate([caja_ancho - pared - 0.1,
                   by(swd_cy) - swd_abertura_ancho/2,
                   pcb_z])
            cube([pared + 0.2, swd_abertura_ancho,
                  base_altura - pcb_z + 0.1]);
        
        // === CANALES SNAP-FIT (dentro de la pared) ===
        
        // Pared frontal: canal cortado desde Y=pared hacia -Y (dentro de la pared)
        translate([pestana_x_izq - pestana_holgura,
                   pared - pestana_saliente - pestana_holgura,
                   canal_z])
            cube([pestana_ancho + 2*pestana_holgura,
                  pestana_saliente + pestana_holgura,
                  canal_altura]);
        translate([pestana_x_der - pestana_holgura,
                   pared - pestana_saliente - pestana_holgura,
                   canal_z])
            cube([pestana_ancho + 2*pestana_holgura,
                  pestana_saliente + pestana_holgura,
                  canal_altura]);
        
        // Pared trasera: canal desde Y=caja_largo-pared hacia +Y
        translate([pestana_x_izq - pestana_holgura,
                   caja_largo - pared,
                   canal_z])
            cube([pestana_ancho + 2*pestana_holgura,
                  pestana_saliente + pestana_holgura,
                  canal_altura]);
        translate([pestana_x_der - pestana_holgura,
                   caja_largo - pared,
                   canal_z])
            cube([pestana_ancho + 2*pestana_holgura,
                  pestana_saliente + pestana_holgura,
                  canal_altura]);
        
        // Hueco para lámina DERECHA (viene de la tapa)
        translate([caja_ancho - pared - lamina_holgura,
                   caja_largo/2 - lamina_largo/2 - lamina_holgura,
                   base_altura - lamina_alto - 0.1])
            cube([lamina_grosor + lamina_holgura + 0.1,
                  lamina_largo + 2*lamina_holgura,
                  lamina_alto + 0.2]);
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
    
   /* // Topes anti-deslizamiento (pegados a pared interior)
    translate([caja_ancho/2 - 3, pared, base_altura - 3])
        cube([6, 1.2, 3]);
    translate([caja_ancho/2 - 3, caja_largo - pared - 1.2, base_altura - 3])
        cube([6, 1.2, 3]);
            */
}

// ============================================================
// TAPA
// ============================================================
module tapa() {
    th = techo_grosor + tapa_interior_h;
    rm = rebaje_margen;
    
    union() {
    difference() {
        union() {
            caja_redondeada(caja_ancho, caja_largo, th, r=2);
            // Lámina alineación DERECHA
            translate([caja_ancho - pared,
                       caja_largo/2 - lamina_largo/2, th])
                cube([lamina_grosor, lamina_largo, lamina_alto]);
        }
        
        // Vaciado
        translate([pared, pared, techo_grosor])
            caja_redondeada(pcb_ancho + 2*pcb_holgura_x,
                            pcb_largo + 2*pcb_holgura_y,
                            tapa_interior_h + 0.1, r=1);
        
        // Hueco lámina IZQUIERDA
        translate([pared/2 - lamina_holgura,
                   caja_largo/2 - lamina_largo/2 - lamina_holgura,
                   th - lamina_alto - 0.1])
            cube([lamina_grosor + lamina_holgura + 0.1,
                  lamina_largo + 2*lamina_holgura,
                  lamina_alto + 0.2]);
        
        // === REBAJES ===
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
        translate([bx(uart_cx) - uart_ranura_ancho/2 - rm,
                   by(uart_cy) - uart_ranura_alto/2 - rm, -0.1])
            caja_redondeada(uart_ranura_ancho + 2*rm,
                            uart_ranura_alto + 2*rm,
                            rebaje_profundidad + 0.1, r=1);
        
        // === ABERTURAS EN SUPERFICIE ===
        
        // LEDs
        translate([bx(led_verde[0]), by(led_verde[1]), -0.1])
            cylinder(d=ventana_led_3mm, h=techo_grosor + 0.2, $fn=24);
        translate([bx(led_rojo[0]), by(led_rojo[1]), -0.1])
            cylinder(d=ventana_led_3mm, h=techo_grosor + 0.2, $fn=24);
        translate([bx(led_pico[0]), by(led_pico[1]), -0.1])
            cylinder(d=ventana_led_pico, h=techo_grosor + 0.2, $fn=24);
        
        // Conectores
        translate([bx(jtag20_cx) - jtag20_ranura_ancho/2,
                   by(jtag20_cy) - jtag20_ranura_alto/2, -0.1])
            cube([jtag20_ranura_ancho, jtag20_ranura_alto, techo_grosor + 0.2]);
        translate([bx(jtag10_cx) - jtag10_ranura_ancho/2,
                   by(jtag10_cy) - jtag10_ranura_alto/2, -0.1])
            cube([jtag10_ranura_ancho, jtag10_ranura_alto, techo_grosor + 0.2]);
        translate([bx(uart_cx) - uart_ranura_ancho/2,
                   by(uart_cy) - uart_ranura_alto/2, -0.1])
            cube([uart_ranura_ancho, uart_ranura_alto, techo_grosor + 0.2]);
        
        // === ABERTURAS EN PAREDES DE LA TAPA ===
        
        // USB (toda la altura de la tapa)
        translate([bx(usb_cx) - usb_abertura_ancho/2,
                   caja_largo - pared - 0.1, 0])
            cube([usb_abertura_ancho, pared + 0.2, th + 0.1]);
        
        // SWD (toda la altura de la tapa)
        translate([caja_ancho - pared - 0.1,
                   by(swd_cy) - swd_abertura_ancho/2, 0])
            cube([pared + 0.2, swd_abertura_ancho, th + 0.1]);
        
        // === ETIQUETAS GRABADAS EN SUPERFICIE ===
        
        translate([bx(jtag10_cx),
                   by(jtag10_cy) + jtag10_ranura_alto/2 + rm + 2,
                   grabado_profundidad])
            mirror([0,0,1]) linear_extrude(grabado_profundidad + 0.1)
                text("JTAG 10", size=texto_mediano, halign="center",
                     valign="center", font="Liberation Sans:style=Bold");
        
        translate([bx(uart_cx),
                   by(uart_cy) + uart_ranura_alto/2 + rm + 2,
                   grabado_profundidad])
            mirror([0,0,1]) linear_extrude(grabado_profundidad + 0.1)
                text("UART", size=texto_mediano, halign="center",
                     valign="center", font="Liberation Sans:style=Bold");
        
        translate([bx(usb_cx), caja_largo - pared - 5, grabado_profundidad])
            mirror([0,0,1]) linear_extrude(grabado_profundidad + 0.1)
                text("USB", size=texto_mediano, halign="center",
                     valign="center", font="Liberation Sans:style=Bold");
        
        translate([bx(led_verde[0]) - ventana_led_3mm/2 - 0.8,
                   by(led_verde[1]), grabado_profundidad])
            mirror([0,0,1]) linear_extrude(grabado_profundidad + 0.1)
                text("G", size=texto_mediano, halign="right",
                     valign="center", font="Liberation Sans:style=Bold");
        
        translate([bx(led_rojo[0]) - ventana_led_3mm/2 - 0.8,
                   by(led_rojo[1]), grabado_profundidad])
            mirror([0,0,1]) linear_extrude(grabado_profundidad + 0.1)
                text("R", size=texto_mediano, halign="right",
                     valign="center", font="Liberation Sans:style=Bold");
        
        translate([bx(led_pico[0]) - ventana_led_pico/2 - 0.8,
                   by(led_pico[1]), grabado_profundidad])
            mirror([0,0,1]) linear_extrude(grabado_profundidad + 0.1)
                text("PWR", size=texto_pequeno, halign="right",
                     valign="center", font="Liberation Sans");
        
        // JTAG 20 grabado en pared frontal (scad Y=0)
        translate([bx(jtag20_cx), grabado_profundidad, th/2])
            rotate([90, 180, 0])
                linear_extrude(grabado_profundidad + 0.1)
                    mirror([1, 0, 0])
                        text("JTAG 20", size=texto_grande, halign="center",
                             valign="center", font="Liberation Sans:style=Bold");
    }
    
    // === PESTAÑAS ARPONCILLO (fuera del difference) ===
    // Pared frontal
    translate([pestana_x_izq, pared, th])
        pestana_arponc();
    translate([pestana_x_der, pared, th])
        pestana_arponc();
    // Pared trasera (espejadas)
    translate([pestana_x_izq, caja_largo - pared - pestana_brazo_grosor, th])
        mirror([0,1,0]) translate([0, -pestana_brazo_grosor, 0]) pestana_arponc();
    translate([pestana_x_der, caja_largo - pared - pestana_brazo_grosor, th])
        mirror([0,1,0]) translate([0, -pestana_brazo_grosor, 0]) pestana_arponc();
    
    } // fin union
    
    // SWD relieve en pared derecha (encima del agujero)
    translate([caja_ancho, by(swd_cy) + swd_abertura_ancho/2 + 4, th/2])
        rotate([270, 0, 90])
            linear_extrude(0.4)
                text("SWD", size=texto_mediano, halign="center",
                     valign="center", font="Liberation Sans:style=Bold");
}

// ============================================================
// ENSAMBLAJE
// ============================================================
separacion = 15;

color("DimGray", 0.85) base();
color("DarkSlateGray", 0.65)
    translate([0, 0, base_altura + separacion])
        mirror([0,0,1])
            translate([0, 0, -tapa_altura])
                tapa();

// PCB referencia
color("Green", 0.2)
    translate([pared + pcb_holgura_x, pared + pcb_holgura_y, pcb_z])
        cube([pcb_ancho, pcb_largo, pcb_grosor]);
// LEDs referencia
color("Lime", 0.4)
    translate([bx(led_verde[0]), by(led_verde[1]), pcb_z + pcb_grosor])
        cylinder(d=3, h=led_th_altura, $fn=16);
color("Red", 0.4)
    translate([bx(led_rojo[0]), by(led_rojo[1]), pcb_z + pcb_grosor])
        cylinder(d=3, h=led_th_altura, $fn=16);

// ============================================================
// NOTAS de la versión
// ============================================================
//
// Dimensiones externas:
//   Ancho: ~45.6mm  |  Largo: ~82.4mm  |  Alto: ~24.6mm
//
// Tornillos: M3 × 8-10mm (solo base)
// ============================================================
