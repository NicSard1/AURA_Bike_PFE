import cv2
from ultralytics import YOLO
from collections import defaultdict, deque
import math

# Chargement du modèle
model = YOLO("yolov8n.pt")

# Classes
DANGER_CLASSES = {"car", "motorcycle", "bus", "truck"}
INFO_CLASSES = {"bicycle"}

# Vidéo d'entrée
INPUT_VIDEO = "video.mp4"
OUTPUT_VIDEO = "aura_bike_ttc_ui_output.mp4"

# Ouvrir la vidéo
cap = cv2.VideoCapture(INPUT_VIDEO)

if not cap.isOpened():
    print("Erreur : impossible d'ouvrir la vidéo.")
    exit()

# Propriétés vidéo
fps = cap.get(cv2.CAP_PROP_FPS)
if fps == 0 or math.isnan(fps):
    fps = 30

width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

# Enregistrement vidéo
fourcc = cv2.VideoWriter_fourcc(*"mp4v")
out = cv2.VideoWriter(OUTPUT_VIDEO, fourcc, fps, (width, height))

# Historique simple des objets par classe et zone
# On ne fait pas un vrai tracking ID ici, mais un suivi léger par "clé logique"
area_history = defaultdict(lambda: deque(maxlen=6))

def get_position(cx, left_limit, right_limit):
    if cx < left_limit:
        return "GAUCHE"
    elif cx > right_limit:
        return "DROITE"
    else:
        return "CENTRE"

def compute_ttc(area_hist, fps_value):
    """
    TTC simplifié basé sur la croissance apparente de la bbox.
    Si l'aire augmente vite, l'objet se rapproche.
    Retourne une estimation en secondes ou None.
    """
    if len(area_hist) < 3:
        return None

    first_area = area_hist[0]
    last_area = area_hist[-1]

    if first_area <= 0:
        return None

    growth_per_frame = (last_area - first_area) / max(len(area_hist) - 1, 1)

    # Si l'objet ne grossit pas, pas de rapprochement détecté
    if growth_per_frame <= 0:
        return None

    # Temps approximatif avant d'atteindre une aire "très proche"
    target_area = 30000
    remaining = target_area - last_area

    if remaining <= 0:
        return 0.5

    ttc_frames = remaining / growth_per_frame
    ttc_seconds = ttc_frames / fps_value

    if ttc_seconds < 0:
        return None

    return min(ttc_seconds, 9.9)

def danger_from_area_and_ttc(area, position, ttc_value):
    """
    Combine proximité apparente + position + TTC simplifié.
    """
    score = 0

    # Proximité apparente
    if area > 4000:
        score += 1
    if area > 12000:
        score += 1

    # Position critique si centré
    if position == "CENTRE":
        score += 1

    # TTC critique
    if ttc_value is not None:
        if ttc_value < 3.0:
            score += 2
        elif ttc_value < 5.0:
            score += 1

    if score <= 1:
        return "LOW", (0, 220, 0), "Vehicule detecte"
    elif score <= 3:
        return "MEDIUM", (0, 165, 255), "Approche detectee"
    else:
        return "HIGH", (0, 0, 255), "Risque de collision"

def draw_direction_indicator(img, position, level):
    """
    Petit indicateur visuel de direction.
    """
    h, w = img.shape[:2]
    y = h - 70

    neutral = (90, 90, 90)
    low = (0, 220, 0)
    med = (0, 165, 255)
    high = (0, 0, 255)

    if level == "HIGH":
        active_color = high
    elif level == "MEDIUM":
        active_color = med
    elif level == "LOW":
        active_color = low
    else:
        active_color = (150, 150, 150)

    left_color = neutral
    center_color = neutral
    right_color = neutral

    if position == "GAUCHE":
        left_color = active_color
    elif position == "CENTRE":
        center_color = active_color
    elif position == "DROITE":
        right_color = active_color

    # Flèches / repères
    cv2.putText(img, "<", (40, y), cv2.FONT_HERSHEY_SIMPLEX, 1.2, left_color, 3)
    cv2.putText(img, "|", (w // 2 - 5, y), cv2.FONT_HERSHEY_SIMPLEX, 1.2, center_color, 3)
    cv2.putText(img, ">", (w - 55, y), cv2.FONT_HERSHEY_SIMPLEX, 1.2, right_color, 3)

while True:
    ret, frame = cap.read()
    if not ret:
        break

    h, w = frame.shape[:2]
    left_limit = int(w * 0.33)
    right_limit = int(w * 0.66)

    annotated = frame.copy()
    results = model(frame, verbose=False)

    # Etat global UI
    global_alert_level = "SAFE"
    global_alert_color = (60, 180, 75)
    global_alert_text = "Route degagee"
    global_position = "-"
    global_ttc = None

    cyclist_info_text = ""
    cyclist_info_color = (255, 200, 0)

    if len(results) > 0:
        result = results[0]
        boxes = result.boxes
        names = result.names

        if boxes is not None:
            for box, cls_id, conf in zip(
                boxes.xyxy.cpu().numpy(),
                boxes.cls.cpu().numpy(),
                boxes.conf.cpu().numpy()
            ):
                class_name = names[int(cls_id)]

                if class_name not in DANGER_CLASSES and class_name not in INFO_CLASSES:
                    continue

                if conf < 0.35:
                    continue

                x1, y1, x2, y2 = map(int, box)
                bw = max(1, x2 - x1)
                bh = max(1, y2 - y1)
                area = bw * bh
                cx = (x1 + x2) // 2

                position = get_position(cx, left_limit, right_limit)

                # -----------------------------
                # VEHICULES MOTORISES
                # -----------------------------
                if class_name in DANGER_CLASSES:
                    # Clé logique simple : classe + zone
                    key = f"{class_name}_{position}"
                    area_history[key].append(area)

                    ttc = compute_ttc(area_history[key], fps)
                    level, color, alert_text = danger_from_area_and_ttc(area, position, ttc)

                    cv2.rectangle(annotated, (x1, y1), (x2, y2), color, 2)

                    if ttc is not None:
                        label = f"{class_name} | {position} | {level} | TTC~{ttc:.1f}s"
                    else:
                        label = f"{class_name} | {position} | {level}"

                    cv2.putText(
                        annotated,
                        label,
                        (x1, max(25, y1 - 8)),
                        cv2.FONT_HERSHEY_SIMPLEX,
                        0.48,
                        color,
                        2
                    )

                    priority = {"SAFE": 0, "LOW": 1, "MEDIUM": 2, "HIGH": 3}
                    if priority[level] > priority[global_alert_level]:
                        global_alert_level = level
                        global_alert_color = color
                        global_alert_text = alert_text
                        global_position = position
                        global_ttc = ttc

                    elif priority[level] == priority[global_alert_level]:
                        # Si même niveau, on prend le TTC le plus court
                        if ttc is not None:
                            if global_ttc is None or ttc < global_ttc:
                                global_alert_color = color
                                global_alert_text = alert_text
                                global_position = position
                                global_ttc = ttc

                # -----------------------------
                # CYCLISTE = INFO SEULEMENT
                # -----------------------------
                elif class_name in INFO_CLASSES:
                    if area > 5000:
                        color = (255, 200, 0)
                        cv2.rectangle(annotated, (x1, y1), (x2, y2), color, 2)

                        label = f"cycliste proche | {position}"
                        cv2.putText(
                            annotated,
                            label,
                            (x1, max(25, y1 - 8)),
                            cv2.FONT_HERSHEY_SIMPLEX,
                            0.48,
                            color,
                            2
                        )

                        cyclist_info_text = f"Cycliste proche - {position}"
                        cyclist_info_color = color

    # Lignes de séparation
    cv2.line(annotated, (left_limit, 0), (left_limit, h), (180, 180, 180), 1)
    cv2.line(annotated, (right_limit, 0), (right_limit, h), (180, 180, 180), 1)

    # Bandeau haut compact
    bar_height = 46
    cv2.rectangle(annotated, (0, 0), (w, bar_height), (25, 25, 25), -1)

    # Pastille de statut
    cv2.circle(annotated, (18, 23), 8, global_alert_color, -1)

    cv2.putText(
        annotated,
        f"Aura Bike | {global_alert_text}",
        (35, 29),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.58,
        (255, 255, 255),
        2
    )

    # Infos à droite
    if global_ttc is not None:
        right_text = f"{global_alert_level} | {global_position} | TTC {global_ttc:.1f}s"
    else:
        right_text = f"{global_alert_level} | {global_position}"

    text_size = cv2.getTextSize(right_text, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 2)[0]
    cv2.putText(
        annotated,
        right_text,
        (w - text_size[0] - 15, 29),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.5,
        (220, 220, 220),
        2
    )

    # Encart d'alerte secondaire en bas à gauche
    if global_alert_level in {"MEDIUM", "HIGH"}:
        box_w = 270
        box_h = 36
        cv2.rectangle(annotated, (12, h - 95), (12 + box_w, h - 95 + box_h), (35, 35, 35), -1)

        alert_line = f"Alerte : {global_alert_text}"
        if global_ttc is not None:
            alert_line += f" ({global_ttc:.1f}s)"

        cv2.putText(
            annotated,
            alert_line,
            (22, h - 70),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.52,
            global_alert_color,
            2
        )

    # Encart cycliste en bas
    if cyclist_info_text:
        cv2.rectangle(annotated, (12, h - 50), (280, h - 14), (40, 40, 40), -1)
        cv2.putText(
            annotated,
            cyclist_info_text,
            (22, h - 25),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.55,
            cyclist_info_color,
            2
        )

    # Indicateur visuel gauche / centre / droite
    draw_direction_indicator(annotated, global_position, global_alert_level)

    # Affichage live
    cv2.imshow("Aura Bike - TTC & Smart UI", annotated)

    # Sauvegarde vidéo
    out.write(annotated)

    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cap.release()
out.release()
cv2.destroyAllWindows()

print(f"Vidéo enregistrée : {OUTPUT_VIDEO}")