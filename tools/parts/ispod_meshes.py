"""Сетки Исподи (docs/ispod.md): имя → функция, которая отдаёт (тело, коробки коллайдера)."""
import ispod
import ispod_props
from plan import colliders


def shell():
    return ispod.shell(), colliders(ispod.collider_ops())


MESHES = {
    "ispod_shell": shell,
    "ispod_tech": lambda: (ispod_props.tech(), ()),
    "ispod_hall": lambda: (ispod_props.hall(), ()),
    "ispod_track": lambda: (ispod_props.track(), ()),
}
